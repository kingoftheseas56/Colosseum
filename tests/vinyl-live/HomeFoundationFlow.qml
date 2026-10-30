import QtQuick
import "../../qml"

// Actual Main, native provider/stores and player. Handler journey, not pointer QA.
// Args: live|playback|restore|offline|inspect, evidence directory.
Main {
    id: root
    readonly property string phase: Qt.application.arguments[2] || "live"
    readonly property string evidence: Qt.application.arguments[3] || ""
    property string step: "profile"
    property real stepAt: Date.now()
    property var wanted: ["Nas","Eminem","Michael Jackson","Prince","Nina Simone"]
    property int picking: 0
    property int discoveryRetries: 0
    property var slidesBefore: []
    property var mixSongs: []
    property string savedId: ""
    property string albumId: ""
    property var radioQueue: []
    property string userQueueId: ""
    property int radioIndex: -1
    property string stationSeed: ""
    property var savedTaste: []
    property real restartPosition: 0
    property string restartTrackId: ""
    property bool albumChecked: false
    function next(value) {step=value;stepAt=Date.now();console.log("VINYL_FOUNDATION_STAGE",phase,value);}
    function fail(message) {
        var w=vinylWorld;
        console.error("VINYL_FOUNDATION_FAIL",phase,step,message,w ? JSON.stringify({page:w.page,
            home:w.recommendationService.homeStatus,error:w.recommendationService.homeError,
            player:w.session.sourceState,playbackError:w.session.error,browseError:w.session.browseError,
            radio:w.recommendationService.homeRadioState,componentReady:w.componentReady,
            enforcingGate:w.enforcingTasteGate,paused:w.session.paused,admitted:w.canEnterVinyl,
            picker:w.worldData.tastePickerOpen,tab:w.worldView.currentTab}) : "no world");
        run.stop();Qt.exit(24);
    }
    function check(condition,message) {if(!condition)fail(message);return !!condition;}
    function slideIds(data) {return data.carouselSlides.map(s=>s.id || s.key);}
    function pass() {
        console.log("VINYL_FOUNDATION_PASS",phase,JSON.stringify({artists:vinylWorld.recommendationService.selectedArtists.length,
            rows:vinylWorld.worldData.homeRows.map(r=>r.key),slides:slideIds(vinylWorld.worldData),
            queue:vinylWorld.session.queue.length,radio:vinylWorld.recommendationService.homeRadioState}));
        if(evidence)contentItem.grabToImage(result=>{result.saveToFile(evidence+"/"+phase+"-home.png");Qt.quit();});
        else Qt.quit();
        run.stop();
    }
    Timer {
        id: run;interval:200;repeat:true;running:true
        onTriggered: {
            if(Date.now()-root.stepAt>120000){root.fail("Stage deadline");return;}
            if(root.step==="profile") {
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;
            var r=w.recommendationService,s=w.session,d=w.worldData;
            if(root.step==="world") {
                if(root.phase==="live") {
                    if(!root.check(!w.canEnterVinyl,"Test profile must begin without artist preferences"))return;
                    w.showLibrary();w.openAlbum("unadmitted-test-route");w.worldView.setTab("discover");
                    if(!root.check(w.page==="home" && !s.browsing && d.tastePickerOpen,"Five-artist gate bypass"))return;
                    if(!root.check(!r.commitTaste(true),"Skip admitted Vinyl"))return;
                    root.next("search");return;
                }
                if(!root.check(w.canEnterVinyl && r.selectedArtists.length>=5,"Five saved tastes did not survive restart"))return;
                w.showHome();root.next("restored-home");return;
            }
            if(root.step==="search") {
                r.searchArtists(root.wanted[root.picking],{});root.next("artist-result");return;
            }
            if(root.step==="artist-result") {
                var result=s.plainData(r.artistResults);
                if(result.query!==root.wanted[root.picking] || result.status==="loading")return;
                if(result.status==="error"){root.fail("Live artist search: "+result.error);return;}
                var artist=(result.items || []).find(a=>String(a.name || a.title).toLowerCase()===root.wanted[root.picking].toLowerCase());
                if(!artist)return;
                if(!root.check(artist.provider==="youtube-music" && /^UC[A-Za-z0-9_-]{22}$/.test(artist.providerId),"Artist identity invalid"))return;
                if(!s.plainData(r.draftArtists).some(selected=>selected.provider===artist.provider && selected.providerId===artist.providerId))r.toggleDraftArtist(artist);
                ++root.picking;
                if(root.picking<5) {
                    if(!root.check(!r.commitTaste(false)&&!w.canEnterVinyl,"Fewer than five artists admitted"))return;
                    root.next("search");return;
                }
                if(!root.check(r.draftArtistCount===5 && r.commitTaste(false),"Five artist save failed"))return;
                w.showHome();root.next("home");return;
            }
            if(root.step==="home" || root.step==="restored-home") {
                if(r.homeStatus==="loading" || !d.homeRows.length)return;
                if(!root.check(d.homeRows.every(row=>!/chart|trending|regional|saved-albums|saved-songs|local-albums|playlists/.test(row.key)),"Unwanted Home feed/Library shelf"))return;
                if(!root.check(w.chromeBackdrop===w.backdrop,"Home wallpaper differs from shell"))return;
                if(!root.check(d.carouselSlides.length>0 && d.carouselSlides.length<=5,"Carousel is empty or exceeds five"))return;
                if(root.phase==="inspect"){run.stop();console.log("VINYL_FOUNDATION_INSPECT_READY");return;}
                if(root.phase==="offline") {
                    if(Date.now()-root.stepAt<18000)return;
                    if(!root.check(d.homeRows.some(row=>row.key.indexOf("personalized:")===0 && row.items.length>0),"Cached catalogue recommendations disappeared offline"))return;
                    if(!root.check(s.paused && s.queue.length>0,"Offline restart did not preserve a paused queue"))return;
                    root.pass();return;
                }
                if(root.phase==="restore") {
                    if(!root.check(w.personalLibrary.playlists.length>0 && s.queue.length>0 && s.paused,"Playlist/queue restart did not restore safely"))return;
                    if(!root.check(s.radioContext.kind==="artist-radio" && r.homeRadioActive,"Radio context missing on restart"))return;
                    if(!root.check(s.lastPlaybackAtMs>0 && d.carouselSlides[0].key==="resume","Qualified session is not the opening spotlight"))return;
                    root.restartPosition=s.position;root.restartTrackId=s.activeQueueRow.trackId;
                    s.paused=false;root.next("restart-playing");return;
                }
                root.slidesBefore=root.slideIds(d);r.beginHomeVisit();root.next("stable-carousel");return;
            }
            if(root.step==="stable-carousel") {
                if(Date.now()-root.stepAt<700 || r.homeStatus==="loading")return;
                if(!root.check(JSON.stringify(root.slideIds(d))===JSON.stringify(root.slidesBefore),"Daily selection changed on revisit"))return;
                r.refreshHomeSelection();root.slidesBefore=root.slideIds(d);root.next("album");return;
            }
            if(root.step==="restart-playing") {
                if(s.error){root.fail("Restart playback: "+s.error);return;}
                if(!s.loaded || s.paused || s.position<root.restartPosition+1)return;
                if(!root.check(s.activeQueueRow.trackId===root.restartTrackId,"Restart resumed a different song"))return;
                console.log("VINYL_FOUNDATION_PLAYING","restart",s.playingTrack.title,s.playingTrack.videoId,s.sourceKind,s.position);
                s.paused=true;s.snapshot();w.showHome();root.next("restart-paused");return;
            }
            if(root.step==="restart-paused" && Date.now()-root.stepAt>800){root.pass();return;}
            if(root.step==="album") {
                var albums=s.plainData(r.homeCarouselItems).filter(item=>item.destination.kind==="album");
                if(!albums.length)return;
                root.albumId=albums[0].destination.providerId;w.activateItem(albums[0]);root.next("album-open");return;
            }
            if(root.step==="album-open") {
                if(s.browsing || w.page!=="album")return;
                if(!root.check(s.tracks.length>0 && s.shownEdition.sourceAlbum.browseId===root.albumId,"Wrong/empty catalogue album"))return;
                console.log("VINYL_FOUNDATION_ALBUM",JSON.stringify({id:s.shownEdition.sourceAlbum.id,
                    browseId:s.shownEdition.sourceAlbum.browseId,origin:s.shownEdition.sourceAlbum.origin,
                    tracks:s.shownEdition.sourceAlbum.tracks.slice(0,2).map(t=>({id:t.id,videoId:t.videoId,available:t.available,keys:Object.keys(t)}))}));
                if(!root.check(w.goBack() && w.page==="home","Album Back lost Home"))return;
                if(!root.check(JSON.stringify(root.slideIds(d))===JSON.stringify(root.slidesBefore),"Album Back changed carousel"))return;
                var related=s.plainData(r.homeSections).find(row=>row.id==="related-discoveries");
                if(related && related.items.length){w.activateItem(related.items[0]);root.next("related-artist");return;}
                root.next("mix");return;
            }
            if(root.step==="related-artist") {
                if(!root.check(w.page==="artist","Related artist route failed"))return;
                w.goBack();root.next("mix");return;
            }
            if(root.step==="mix") {
                var card=s.plainData(r.homeListeningCards).find(item=>item.mixId==="personal");
                if(!card){r.openHomeMix("personal");root.next("mix-probe");return;}
                w.activateItem(card);root.next("mix-view");return;
            }
            if(root.step==="mix-probe") {
                var probe=s.plainData(r.homeMixView);
                if(probe.status==="loading")return;
                if(!root.check(probe.status==="ready","Mix preparation failed: "+probe.error))return;
                var prepared=s.plainData(r.homeListeningCards).find(item=>item.mixId==="personal");
                if(!root.check(!!prepared,"Prepared mix card is not bound to Home"))return;
                w.activateItem(prepared);root.next("mix-view");return;
            }
            if(root.step==="mix-view") {
                var view=s.plainData(r.homeMixView);
                if(view.status==="loading")return;
                if(!root.check(w.page==="mix"&&view.songs.length>0&&view.songs.length<=12,"Mix failed: "+view.error))return;
                root.mixSongs=view.songs;
                if(!root.check(root.mixSongs.every(song=>song.album.tracks[song.index].id===song.track.id),"Mix lost original song identity"))return;
                root.savedId=w.personalLibrary.createPlaylist("Foundation personal mix",s.rowsForSongs(root.mixSongs));
                if(!root.check(!!root.savedId,"Mix snapshot did not save to Library"))return;
                root.next("open-discovery");return;
            }
            if(root.step==="open-discovery") {
                var discovery=s.plainData(r.homeListeningCards).find(item=>item.mixId==="discovery");
                if(!discovery)return;
                console.log("VINYL_FOUNDATION_DISCOVERY_SEEDS",JSON.stringify(s.plainData(r.homeSections)
                    .filter(row=>row.id==="related-discoveries").map(row=>row.items.slice(0,2).map(item=>({title:item.title,destination:item.destination})))));
                w.activateItem(discovery);root.next("discovery-mix");return;
            }
            if(root.step==="discovery-mix") {
                var discoveryView=s.plainData(r.homeMixView);
                if(discoveryView.status==="loading")return;
                if(discoveryView.status==="error" && root.discoveryRetries++===0) {
                    console.log("VINYL_FOUNDATION_RETRY","discovery",discoveryView.error);
                    r.openHomeMix("discovery");root.stepAt=Date.now();return;
                }
                if(!root.check(discoveryView.id==="discovery"&&discoveryView.songs.length>0,"Discovery mix failed: "+discoveryView.error))return;
                if(!root.check(discoveryView.songs.every(song=>song.album.tracks[song.index].id===song.track.id),"Discovery mix lost exact songs"))return;
                if(!root.check(s.playHomeSongs(discoveryView.songs,0,false,{}),"Discovery mix could not start"))return;
                root.next("discovery-playing");return;
            }
            if(root.step==="discovery-playing") {
                if(s.error){root.fail(s.error);return;}
                if(!s.loaded || s.paused || s.position<1)return;
                console.log("VINYL_FOUNDATION_PLAYING","discovery",s.playingTrack.title,s.playingTrack.videoId,s.sourceKind,s.position);
                if(!root.check(s.playHomeSongs(root.mixSongs,0,false,{}),"Personal mix could not start"))return;
                root.next("mix-playing");return;
            }
            if(root.step==="mix-playing") {
                if(s.error){root.fail(s.error);return;}
                if(!s.loaded || s.paused || s.position<2 || !s.lastPlaybackAtMs)return;
                if(!root.check(s.queue.length===root.mixSongs.length && s.activeQueueRow.trackId===root.mixSongs[0].track.id,"Player loaded the wrong mix song"))return;
                if(!root.check(s.activeQueueRow.track.videoId===root.mixSongs[0].track.videoId,"Queue projection lost the exact audio identity needed by radio deduplication"))return;
                console.log("VINYL_FOUNDATION_PLAYING","personal",s.playingTrack.title,s.playingTrack.videoId,s.sourceKind,s.position);
                s.seek(20);root.next("seek");return;
            }
            if(root.step==="seek") {
                if(s.seekPending || s.position<19)return;
                s.paused=true;w.showHome();
                if(!root.check(d.carouselSlides[0].key==="resume","Active session did not lead Home"))return;
                w.openHomeDestination("library","playlists",root.savedId);
                w.personalLibrary.playPlaylist(root.savedId,false);root.next("playlist-playing");return;
            }
            if(root.step==="playlist-playing") {
                if(s.error){root.fail(s.error);return;}
                if(!s.loaded || s.paused || s.position<1)return;
                if(!root.check(s.queue.length===root.mixSongs.length,"Saved mix playback lost tracks"))return;
                s.paused=true;w.showHome();
                var station=s.plainData(r.homeListeningCards).find(item=>item.kind==="radio");
                if(!root.check(!!station,"Artist radio card absent"))return;
                root.stationSeed=station.seedArtist.providerId;w.activateItem(station);root.next("radio-playing");return;
            }
            if(root.step==="radio-playing") {
                if(r.homeRadioState==="error" || s.error){root.fail("Radio playback failed");return;}
                if(!s.loaded || s.paused || s.position<1 || s.radioContext.kind!=="artist-radio")return;
                if(!root.check(s.radioContext.seed.providerId===root.stationSeed,"Radio seed changed"))return;
                console.log("VINYL_FOUNDATION_PLAYING","radio",s.playingTrack.title,s.playingTrack.videoId,s.sourceKind,s.position);
                s.appendHomeSongs([root.mixSongs[0]],false);
                root.userQueueId=s.queue[s.queue.length-1].queueId;
                root.radioQueue=s.queue.map(row=>row.queueId);
                root.radioIndex=Math.max(0,s.queue.length-2);s.playQueue(root.radioIndex);
                root.next("radio-refill");return;
            }
            if(root.step==="radio-refill") {
                if(r.homeRadioState==="error"){root.fail("Station refill failed");return;}
                if(s.queue.length<=root.radioQueue.length || !s.loaded || s.paused || s.duration<10)return;
                if(!root.check(s.queue.slice(0,root.radioQueue.length).map(row=>row.queueId).join("|")===root.radioQueue.join("|"),"Radio refill replaced user queue"))return;
                if(!root.check(s.queue.some(row=>row.queueId===root.userQueueId),"User queued song disappeared"))return;
                s.seek(s.duration-2);root.next("radio-natural-end");return;
            }
            if(root.step==="radio-natural-end") {
                if(s.queueIndex<=root.radioIndex || !s.loaded || s.paused || s.position<1 || !s.lastPlaybackAtMs)return;
                root.savedTaste=s.plainData(ProfilePreferences.vinylTasteArtists);
                if(!root.check(ProfilePreferences.setVinylTasteSelections(true,root.savedTaste.slice(0,4)),"Could not test four saved choices"))return;
                root.next("revoked-admission");return;
            }
            if(root.step==="revoked-admission") {
                if(w.canEnterVinyl || !d.tastePickerOpen)return;
                if((!s.paused || w.page!=="home") && Date.now()-root.stepAt<2000)return;
                if(!root.check(s.paused && w.page==="home","Revoked admission left music playing or a detail route accessible"))return;
                if(!root.check(ProfilePreferences.setVinylTasteSelections(true,root.savedTaste),"Could not restore five saved artists"))return;
                root.next("readmit-radio");return;
            }
            if(root.step==="readmit-radio") {
                if(!w.canEnterVinyl)return;
                var admittedStation=s.plainData(r.homeListeningCards).find(item=>item.kind==="radio" && item.seedArtist.providerId===root.stationSeed);
                if(!admittedStation)return;
                w.activateItem(admittedStation);root.next("radio-after-gate");return;
            }
            if(root.step==="radio-after-gate") {
                if(r.homeRadioState==="error" || s.error){root.fail("Radio after admission failed");return;}
                if(!s.loaded || s.paused || s.position<2 || !s.lastPlaybackAtMs || s.radioContext.kind!=="artist-radio")return;
                s.paused=true;s.snapshot();w.showHome();root.next("finish");return;
            }
            if(root.step==="finish" && Date.now()-root.stepAt>800)root.pass();
        }
    }
}
