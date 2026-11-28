import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylHudContext"
    when: windowShown
    property var host
    property var viewed
    property var bar
    // Exercise the production HUD and popup directly; the world prototype imports
    // catalogue fixtures which are not part of this isolated QML source checkout.
    Component {
        id: hudFactory
        Window {
            id: hudWindow
            width: 1280; height: 720; visible: true
            property alias pageData: playback
            property string page: "song"
            property bool videoFullscreen: false
            property bool ready: true
            property bool browsingSong: true
            property int videoRoutes: 0
            property string videoTargetTitle: ""
            Vinyl.VinylPageData {
                id: playback
                frozen: true
                property string sourceKind: "youtube"
                property bool loaded: true
                property bool torrentAvailable: true
                property bool torrentBusy: false
                property string torrentError: ""
                property bool videoBusy: false
                property bool videoSwitchCancellable: false
                property string videoError: ""
                property int youtubeRequests: 0
                property int torrentRequests: 0
                function findTorrent() { torrentRequests++; torrentBusy = true }
                function selectYouTube() { youtubeRequests++; torrentBusy = false; sourceKind = "youtube"; sourceState = "playing" }
            }
            Item { id: backdrop; anchors.fill: parent }
            Vinyl.VinylSongPage {
                id: songPage
                anchors.fill: parent
                anchors.bottomMargin: hud.height + 12
                pageData: hudWindow.browsingSong ? hud.songData : playback
                browseMode: hudWindow.browsingSong && hud.showSongPlay
                visible: hudWindow.page === "song"
                videoFullscreen: hudWindow.videoFullscreen
                onVideoFullscreenRequested: hudWindow.videoFullscreen = !hudWindow.videoFullscreen
                onPlayRequested: hud.selectedSongPlayRequested()
            }
            Vinyl.VinylPlayerBar {
                id: hud
                width: hudWindow.width
                y: hudWindow.height - height
                pageData: playback
                page: hudWindow.page
                backdrop: backdrop
                videoFullscreen: hudWindow.videoFullscreen
                onVideoRequested: {
                    hudWindow.browsingSong=false;
                    hudWindow.page="song";
                    hudWindow.videoRoutes++;
                    hudWindow.videoTargetTitle=playback.playingTrack.title;
                    playback.toggleVideo();
                }
                onVideoFullscreenRequested: hudWindow.videoFullscreen = !hudWindow.videoFullscreen
            }
        }
    }
    Component {
        id: viewedFactory
        Vinyl.VinylPageData {
            frozen: true
            property bool personalEnabled: true
            property bool saved: false
            property int playlists: 0
            property int enqueues: 0
            property bool videoBusy: false
            property bool videoSwitchCancellable: false
            function songIsSaved() { return saved; }
            function togglePlayingSongSaved() { saved = !saved; }
            function addPlayingSongToPlaylist() { playlists++; }
            function enqueuePlayingSong(next) { enqueues++; }
        }
    }
    Component {
        id: emptyPlaybackFactory
        QtObject {
            property int playingIndex: -1
            property var playingTrack: ({title:""})
            property var playingEdition: ({id:"",name:"",artist:"",cover:""})
            property string sourceState: "idle"
            property string sourceKind: ""
            property bool paused: true
            property bool canNext: false
            property bool canPrev: false
            property real position: 0
            property real duration: 0
            function artwork(url) { return url || ""; }
            function formatTime(value) { return "0:00"; }
        }
    }
    SignalSpy { id: queueSpy; target: test.bar ? test.bar.pageData : null; signalName: "queueRequested" }
    SignalSpy { id: playSpy; target: test.bar; signalName: "selectedSongPlayRequested" }
    function item(name) {
        return findChild(host,name);
    }
    function countNamed(parent, name) {
        var count = parent.objectName === name ? 1 : 0;
        var children = parent.children || [];
        for(var i=0;i<children.length;i++) count += countNamed(children[i],name);
        return count;
    }
    function init() {
        host=hudFactory.createObject(null);
        verify(host !== null);
        host.pageData.frozen=true;host.pageData.setFixture("thriller");host.page="song";
        tryCompare(host,"ready",true);
        viewed=viewedFactory.createObject(host.contentItem);
        viewed.setFixture("without-me");
        bar=findChild(host,"vinylPlayerBar");
        bar.songPageData=viewed;bar.showSongPlay=true;
        waitForRendering(host.contentItem);
        playSpy.clear();queueSpy.clear();
    }
    function cleanup() {
        if(host){host.close();host.destroy();}
        bar=null;viewed=null;host=null;
    }
    function test_readerToolsUseViewedSongWithoutChangingPlayback() {
        var owner=bar.pageData, key=bar.currentSeekTrackKey, position=owner.position;
        verify(!owner.lyricsShown);verify(!viewed.lyricsShown);
        mouseClick(item("vinylBarLyrics"));
        verify(viewed.lyricsShown);verify(!owner.lyricsShown);
        mouseClick(item("vinylBarNotes"));
        verify(viewed.autoNotes);verify(!owner.autoNotes);
        compare(bar.pageData,owner);compare(owner.position,position);compare(bar.currentSeekTrackKey,key);
        verify(findChild(host,"vinylSongBrowseActions")===null);
    }
    function test_firstViewedSongCanPlayWithoutAnExistingSession() {
        var empty=emptyPlaybackFactory.createObject(host.contentItem);
        bar.pageData=empty;
        verify(!bar.hasPlayingSong);
        compare(bar.displayedSong,empty);
        verify(!item("vinylBarPlay").enabled);
        verify(item("vinylSongPlay").enabled);
        mouseClick(item("vinylSongPlay"));
        compare(playSpy.count,1);verify(empty.paused);
        verify(!item("vinylBarSeek").enabled);
    }
    function test_fullscreenControlSignalsWithoutChangingPlayback() {
        viewed.videoShown=true;
        var owner=bar.pageData, position=owner.position, paused=owner.paused;
        verify(item("vinylBarFullscreen").visible);
        compare(item("vinylBarFullscreen").accessibleName,"Enter fullscreen");
        waitForRendering(host.contentItem);
        mouseClick(item("vinylBarFullscreen"));
        compare(host.videoFullscreen,true);
        compare(bar.videoFullscreen,true);
        compare(item("vinylBarFullscreen").accessibleName,"Exit fullscreen");
        mouseClick(item("vinylBarFullscreen"));
        compare(host.videoFullscreen,false);
        compare(owner.position,position);
        compare(owner.paused,paused);
        compare(bar.songData,viewed);
        compare(bar.pageData,owner);
    }
    function test_collectionActionsUseViewedSongAndQueueUsesPlayback() {
        var owner=bar.pageData, key=bar.currentSeekTrackKey;
        waitForRendering(host.contentItem);
        mouseClick(item("vinylSongSave"));
        verify(viewed.saved);
        mouseClick(item("vinylSongPlaylist"));
        compare(viewed.playlists,1);
        compare(findChild(item("vinylSongPage"),"vinylSongNext"),null);
        compare(findChild(item("vinylSongPage"),"vinylSongQueue"),null);
        mouseClick(item("vinylBarQueue"));
        compare(queueSpy.count,1);
        compare(bar.pageData,owner);
        compare(bar.currentSeekTrackKey,key);
    }
    function test_videoRequestsRouteToPlayingSongBeforeToggling() {
        var owner=bar.pageData;
        host.page="album";
        verify(host.browsingSong);
        verify(item("vinylSongPage").pageData===viewed);
        viewed.videoShown=true;
        owner.videoShown=false;
        mouseClick(item("vinylBarVideo"));
        compare(host.page,"song");verify(!host.browsingSong);
        compare(host.videoRoutes,1);
        compare(host.videoTargetTitle,owner.playingTrack.title);
        compare(item("vinylSongPage").pageData,owner);
        verify(owner.videoShown);verify(viewed.videoShown);
        mouseClick(item("vinylBarVideo"));
        verify(!owner.videoShown);verify(viewed.videoShown);
        compare(host.videoRoutes,2);
        compare(bar.pageData,owner);
    }
    function test_transportAndScrubKeepPlayingContext() {
        var owner=bar.pageData, start=owner.position, viewedPosition=viewed.position;
        mouseClick(item("vinylBarPlay"));
        verify(owner.paused);
        verify(!viewed.paused);
        var seek=item("vinylBarSeek");
        bar.beginScrub(seek.width / 4);
        verify(bar.scrubbing);
        compare(owner.position,start);
        bar.commitScrub(seek.width / 2);
        verify(!bar.scrubbing);
        compare(owner.position,owner.duration / 2);
        compare(viewed.position,viewedPosition);
        bar.beginScrub(0);
        bar.cancelScrub();
        compare(owner.position,owner.duration / 2);
    }
    function test_seekKeyboardAndTrackChangeKeepPlayingIdentity() {
        var owner=bar.pageData,viewedPosition=viewed.position,seek=item("vinylBarSeek");
        owner.position=40;seek.forceActiveFocus();keyClick(Qt.Key_Right);
        compare(owner.position,45);compare(viewed.position,viewedPosition);
        bar.beginScrub(seek.width*0.8);verify(bar.scrubbing);
        var oldKey=bar.currentSeekTrackKey;
        owner.next(1);verify(bar.currentSeekTrackKey!==oldKey);verify(!bar.scrubbing);
        var newPosition=owner.position;bar.commitScrub(seek.width*0.8);compare(owner.position,newPosition);
    }
    function test_sourceReadinessChangeCancelsScrubWithoutLateCommit() {
        var owner=bar.pageData,position=owner.position,seek=item("vinylBarSeek");
        bar.beginScrub(seek.width*0.8);verify(bar.scrubbing);
        owner.sourceState="resolving";verify(!bar.scrubbing);verify(!seek.enabled);
        owner.sourceState="playing";bar.commitScrub(seek.width*0.8);
        compare(owner.position,position);
        bar.beginScrub(seek.width*0.4);seek.forceActiveFocus();keyClick(Qt.Key_Escape);
        verify(!bar.scrubbing);compare(owner.position,position);
    }
    function test_albumRetainsTransportAndQueue() {
        bar.page="album";
        waitForRendering(host.contentItem);
        compare(bar.height,72);
        var play=item("vinylBarPlay");
        compare(play.mapToItem(bar,play.width/2,0).x,bar.transportCenter);
        verify(item("vinylBarQueue").visible);
        compare(findChild(bar,"vinylSongSave"),null);
        verify(findChild(bar,"vinylBarVideo")!==null);
        mouseClick(item("vinylBarQueue"));
        compare(queueSpy.count,1);
    }
    function test_barIsSlimOnEveryBrowsingWidth_data() {
        return [{tag:"desktop",width:1280},{tag:"actual",width:1028},{tag:"medium",width:800},
            {tag:"desktop-minimum",width:700},{tag:"below-desktop-minimum",width:699},
            {tag:"phone",width:360},{tag:"wide-phone",width:480}];
    }
    function test_barIsSlimOnEveryBrowsingWidth(data) {
        bar.width = data.width;
        host.page = "album";
        verify(bar.height <= 80);
        compare(item("vinylBarOpenSong").width, 44);
        compare(item("vinylBarSeek").y, 0);
        for (const name of ["vinylBarSources", "vinylBarYouTube", "vinylBarTankorent", "vinylBarClose"])
            tryVerify(() => findChild(bar, name) === null, 1000, name + " must not exist on a browsing page");
        verify(item("vinylBarMinimise").visible);
        compare(item("vinylBarMinimise").accessibleName, "Minimise");

        host.page = "song";
        const wide = data.width >= 700;
        compare(bar.height, wide ? 104 : 164);
        compare(item("vinylBarSeek").y, wide ? 69 : 128);
        verify(item("vinylBarSources").visible);
        verify(item("vinylBarYouTube").visible);
        verify(item("vinylBarTankorent").visible);
        tryVerify(() => findChild(bar, "vinylBarMinimise") === null);
        compare(findChild(bar,"vinylSongSave"),null);
        verify(findChild(bar,"vinylBarVideo")!==null);
        compare(findChild(bar,"vinylBarLyrics"),null);
        compare(findChild(bar,"vinylSongPlay"),null);
        verify(item("vinylSongPageActions").visible);
        compare(item("vinylSongSave").accessibleDescription,"For "+viewed.playingTrack.title);
        compare(countNamed(host.contentItem,"vinylSongSave"),1);
    }
    function test_resolvingRetainsIdentityAndDisablesTransportUntilReady() {
        var owner=bar.pageData,track=owner.playingTrack,key=bar.currentSeekTrackKey;
        owner.sourceState="resolving";
        verify(bar.busy);
        verify(item("vinylBarLoading").running);
        verify(!item("vinylBarPlay").enabled);
        verify(!item("vinylBarSeek").enabled);
        compare(bar.displayedSong,owner);
        compare(owner.playingTrack,track);
        compare(bar.currentSeekTrackKey,key);
        compare(item("vinylBarNowPlaying").accessibleName,"Now playing: "+track.title);
        owner.sourceState="playing";
        verify(!bar.busy);
        verify(!item("vinylBarLoading").running);
        verify(item("vinylBarPlay").enabled);
        verify(item("vinylBarSeek").enabled);
        compare(owner.playingTrack,track);
        compare(bar.currentSeekTrackKey,key);
    }
    function test_slimProgressHoversAndSeeks() {
        host.page = "album";
        const owner = bar.pageData, seek = item("vinylBarSeek");
        const pointer = item("vinylBarSeekPointer"), line = item("vinylBarProgressTrack");
        const original = owner.position;
        mouseMove(bar, 40, bar.height - 3);
        compare(line.height, 3);
        mouseMove(pointer, 40, 4);
        tryCompare(line, "height", 6);
        mousePress(pointer, pointer.width / 4, 4);
        verify(bar.scrubbing);
        mouseMove(pointer, pointer.width / 2, 4);
        compare(owner.position, original, "Drag only previews the position");
        compare(bar.displayedPosition, owner.duration / 2);
        mouseRelease(pointer, pointer.width / 2, 4);
        compare(owner.position, owner.duration / 2);
        const quarterX = Math.round(pointer.width / 4);
        const quarterPosition = quarterX / pointer.width * owner.duration;
        mouseClick(pointer, quarterX, 4);
        compare(owner.position, quarterPosition);
        seek.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(owner.position, quarterPosition + 5);
        verify(item("vinylBarArtist").text.includes(" · ") && item("vinylBarArtist").text.includes(" / "));
    }
    function test_slimVideoOnlyAppearsWhenAvailable() {
        host.page = "album";
        verify(item("vinylBarVideo").visible);
        host.pageData.playingEdition = Object.assign({}, host.pageData.playingEdition,
            {hasVideo: false, artist: "Fixture artist"});
        verify(!host.pageData.hasVideo);
        verify(!item("vinylBarVideo").visible);
        host.page = "song";
        verify(item("vinylBarVideo").visible);
        verify(!item("vinylBarVideo").enabled);
    }

    function test_controlsDoNotOverlap_data() {
        var rows = [{tag:"phone",width:360,height:164,page:"song"},
            {tag:"wide-phone",width:480,height:164,page:"song"},
            {tag:"wide-slim",width:1280,height:72,page:"home"},
            {tag:"with-dock",width:966,height:72,page:"home"}];
        for (const width of [306, 360, 700, 760, 860, 1000]) {
            rows.push({tag:"slim-" + width, width:width, height:72, page:"home"});
            if (width < 700) continue;
            rows.push({tag:"song-" + width, width:width, height:104, page:"song", notice:"No torrent found"});
        }
        return rows;
    }
    function test_controlsDoNotOverlap(data) {
        bar.width=data.width;
        host.page=data.page;
        host.pageData.torrentError=data.notice || "";
        waitForRendering(host.contentItem);
        compare(bar.height,data.height);
        var names=["vinylBarOpenSong","vinylBarNowPlaying","vinylBarPrev","vinylBarPlay",
            "vinylBarNext","vinylBarVideo","vinylBarQueue","vinylBarSeek"];
        if (data.page === "song") names.push("vinylBarSources", "vinylBarElapsed", "vinylBarDuration");
        else names.push("vinylBarMinimise");
        if(data.notice) names.push("vinylBarSourceError");
        var bounds=[];
        for (const name of names) {
            var control=findChild(bar,name),pos=control.mapToItem(bar,0,0);
            verify(control.visible,name+" remains visible");
            verify(control.width>0 && pos.x>=0 && pos.x+control.width<=bar.width,name+" fits width");
            verify(control.height>0 && pos.y>=0 && pos.y+control.height<=bar.height,name+" fits height");
            bounds.push({name:name,x:pos.x,y:pos.y,w:control.width,h:control.height});
        }
        for(var j=0;j<bounds.length;j++)for(var k=j+1;k<bounds.length;k++) {
            var a=bounds[j],b=bounds[k];
            verify(a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y,
                a.name+" must not overlap "+b.name);
        }
        if (!bar.narrow) {
            const identity = item("vinylBarNowPlaying");
            const title = item("vinylBarTitle"), artist = item("vinylBarArtist");
            for (const label of [title, artist]) {
                const p = label.mapToItem(identity, 0, 0);
                verify(label.visible && label.text.length > 0);
                verify(p.x >= 0 && p.x + label.width <= identity.width);
                verify(p.y >= 0 && p.y + label.height <= identity.height);
            }
            verify(title.y + title.height <= artist.y);
            verify(!artist.truncated, "Artist fixture fits without elision");
            if (data.page !== "song") return;
            const yt = item("vinylBarYouTube"), tankorent = item("vinylBarTankorent");
            compare(yt.x + yt.width, tankorent.x);
            compare(tankorent.x + tankorent.width, item("vinylBarSources").width);
            for (const control of [yt, tankorent]) {
                const label = item(control.objectName + "Label");
                verify(control.visible && label.visible && label.text.length > 0);
                verify(label.x + label.implicitWidth <= control.width);
            }
            if (data.notice)
                verify(!item("vinylBarSourceError").truncated);
            console.info("BAR_GEOMETRY " + data.tag + " " + JSON.stringify(bounds));
        }
    }
    function test_longTimelineSpansCapsuleBelowAllControls_data() {
        return [{tag:"desktop",width:1280},{tag:"actual",width:1028},{tag:"tablet",width:800},
            {tag:"phone",width:360},{tag:"wide-phone",width:480}];
    }
    function test_longTimelineSpansCapsuleBelowAllControls(data) {
        bar.width=data.width;waitForRendering(host.contentItem);
        var seek=item("vinylBarSeek"),elapsed=item("vinylBarElapsed"),duration=item("vinylBarDuration");
        verify(seek.x<=64,"Timeline begins near the left inner edge");
        verify(seek.x+seek.width>=bar.width-64,"Timeline reaches the right inner edge");
        verify(seek.width>bar.width*0.65,"Timeline spans most of the capsule");
        for(var name of ["vinylBarOpenSong","vinylBarNowPlaying","vinylBarTransport","vinylBarVideo","vinylBarQueue","vinylBarSources","vinylBarSourceError"]) {
            var control=item(name),point=control.mapToItem(bar,0,0);
            verify(point.y+control.height<=seek.y,name+" stays above the timeline");
        }
        verify(elapsed.mapToItem(bar,0,0).x>=17);
        verify(duration.mapToItem(bar,duration.width,0).x<=bar.width-17);
        compare(item("vinylBarPlay").mapToItem(bar,item("vinylBarPlay").width/2,0).x,bar.transportCenter);
    }
    function test_sourceUsesOneContiguousCapsuleAndSolidSelectedHalf() {
        var pill=item("vinylBarSources"),yt=item("vinylBarYouTube"),torrent=item("vinylBarTankorent");
        var capsule=item("vinylBarSourceCapsule");
        compare(capsule.parent,pill);compare(capsule.width,pill.width);compare(capsule.radius,17);
        compare(yt.x,0);compare(yt.x+yt.width,torrent.x);compare(torrent.x+torrent.width,pill.width);
        compare(yt.height,pill.height);compare(torrent.height,pill.height);
        var selected=item("vinylBarYouTubeSelection"),label=item("vinylBarYouTubeLabel");
        verify(selected.visible);compare(selected.color.a,1);compare(selected.border.width,0);
        verify(!item("vinylBarTankorentSelection").visible);
        verify(label.color.r<selected.color.r && label.color.g<selected.color.g,"Selected source uses a dark label on gold");
        var width=pill.width;
        bar.pageData.torrentBusy=true;
        compare(pill.width,width);verify(yt.selected);verify(torrent.pending);verify(!torrent.selected);
    }
    function test_currentSongHasNoDuplicatePlay() {
        bar.showSongPlay=false;
        compare(item("vinylSongPlay").visible,false);
        verify(item("vinylBarPlay").enabled);
        mouseClick(item("vinylBarPlay"));
        compare(playSpy.count,0);
        verify(bar.pageData.paused);
    }
    function test_viewedPlayIsExplicitAndArtworkOnlyOpensSong() {
        var owner=bar.pageData,paused=owner.paused;
        mouseClick(item("vinylSongPlay"));
        compare(playSpy.count,1);compare(owner.paused,paused);
        var opens=0;bar.songRequested.connect(function(){opens++;});
        mouseClick(item("vinylBarOpenSong"));
        compare(opens,1);compare(playSpy.count,1);
    }
    function test_onlyThumbnailNavigatesWhileMetadataSeekAndQueueStayInPlace() {
        var opens=0;bar.songRequested.connect(function(){opens++;});
        var metadata=item("vinylBarNowPlaying"),position=bar.pageData.position;
        verify(!metadata.activeFocusOnTab);
        mouseClick(metadata,metadata.width/2,metadata.height/2);
        compare(opens,0);compare(bar.pageData.position,position);
        var pointer=item("vinylBarSeekPointer");
        mouseClick(pointer,pointer.width/2,pointer.height/2);
        compare(opens,0);compare(bar.pageData.position,bar.pageData.duration/2);
        mouseClick(item("vinylBarQueue"));compare(opens,0);compare(queueSpy.count,1);
        mouseClick(item("vinylBarOpenSong"));compare(opens,1);
    }
    function test_pageVideoToolsFollowAutoHideIncludingFullscreen() {
        var page=item("vinylSongPage");
        viewed.videoShown=true;host.videoFullscreen=true;
        page.videoControlsVisible=false;
        tryCompare(item("vinylSongPageActions"),"visible",false);
        tryCompare(item("vinylSongBack"),"visible",false);
        page.videoControlsVisible=true;
        tryCompare(item("vinylSongPageActions"),"visible",true);
        tryCompare(item("vinylSongBack"),"visible",true);
        verify(item("vinylBarFullscreen").visible);
        item("vinylBarFullscreen").forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.videoFullscreen,false);
    }
    function test_sourcesUsePlayingSessionAndOnlyHighlightAppliedReadyAudio() {
        var owner=bar.pageData,yt=item("vinylBarYouTube"),torrent=item("vinylBarTankorent");
        verify(yt.selected);verify(!torrent.selected);
        var pillWidth=item("vinylBarSources").width;
        mouseClick(torrent);
        compare(owner.torrentRequests,1);verify(owner.torrentBusy);
        verify(yt.selected);verify(!torrent.selected);verify(torrent.pending);
        compare(item("vinylBarSources").width,pillWidth);
        owner.torrentBusy=false;owner.sourceKind="torrent";owner.sourceState="resolving";
        verify(!yt.selected);verify(!torrent.selected);
        owner.sourceState="playing";
        verify(torrent.selected);verify(!yt.selected);
        mouseClick(yt);
        compare(owner.youtubeRequests,1);verify(yt.selected);verify(!torrent.selected);
        mouseClick(torrent);verify(owner.torrentBusy);
        mouseClick(yt);verify(!owner.torrentBusy);verify(yt.selected);
        owner.torrentBusy=true;owner.torrentError="No torrent found";owner.torrentBusy=false;
        verify(yt.selected);verify(!torrent.selected);
        compare(item("vinylBarSourceError").text,"No torrent found");
        owner.videoShown=true;verify(!yt.enabled);verify(!torrent.enabled);
        owner.videoShown=false;owner.videoBusy=true;verify(!yt.enabled);verify(!torrent.enabled);
        owner.videoBusy=false;owner.sourceKind="local";verify(!yt.enabled);verify(!torrent.enabled);
        compare(bar.songData,viewed);
    }
    function test_initialUnresolvedAudioCannotSelectSourcesButPendingCanCancel() {
        var owner=bar.pageData,yt=item("vinylBarYouTube"),torrent=item("vinylBarTankorent");
        owner.loaded=false;owner.sourceState="resolving";
        verify(!bar.sourceReady);verify(!yt.selected);verify(!torrent.selected);
        verify(!yt.enabled);verify(!torrent.enabled);verify(yt.pending);
        yt.activate();torrent.activate();compare(owner.youtubeRequests,0);compare(owner.torrentRequests,0);
        owner.sourceState="playing";verify(!bar.sourceReady);verify(!yt.enabled);verify(!torrent.enabled);
        owner.loaded=true;verify(yt.enabled);verify(torrent.enabled);
        torrent.activate();verify(owner.torrentBusy);verify(yt.enabled);verify(!torrent.enabled);
        yt.activate();compare(owner.youtubeRequests,1);verify(!owner.torrentBusy);verify(yt.selected);
    }
    function test_restoredRemoteDefaultsToYouTubeUntilPlaybackIsConfirmed() {
        var owner=bar.pageData,yt=item("vinylBarYouTube"),torrent=item("vinylBarTankorent");
        owner.sourceKind="";owner.sourceState="paused";owner.loaded=false;
        verify(bar.defaultYouTubeChoice);verify(yt.selected);verify(!yt.enabled);
        verify(!torrent.selected);verify(!torrent.enabled);
        compare(yt.accessibleDescription,"YouTube is the default choice; current playback source is not confirmed");
        owner.sourceKind="youtube";owner.sourceState="playing";owner.loaded=true;
        verify(!bar.defaultYouTubeChoice);verify(yt.selected);
        compare(yt.accessibleDescription,"Current audio source");
        owner.sourceKind="";owner.sourceState="paused";owner.loaded=false;
        owner.playingEdition=Object.assign({},owner.playingEdition,{origin:"local"});
        verify(bar.localSong);verify(!bar.defaultYouTubeChoice);verify(!yt.selected);
    }
    function test_oneActualVideoActionAndReadablePageTools() {
        compare(countNamed(host.contentItem,"vinylBarVideo"),1);
        compare(findChild(item("vinylSongPage"),"vinylBarVideo"),null);
        verify(item("vinylBarNotes").iconSource.toString().endsWith("genius.png"));
        var collection=item("vinylSongAddToCollections");
        compare(collection.labelText,"Add to Collection");
        verify(!collection.ToolTip.visible);
        compare(collection.objectName,"vinylSongAddToCollections");
    }
}
