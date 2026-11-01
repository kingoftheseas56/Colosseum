import QtQuick
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
            Vinyl.VinylPageData { id: playback; frozen: true }
            Item { id: backdrop; anchors.fill: parent }
            Vinyl.VinylCreditsPopup {
                id: credits
                parent: hudWindow.contentItem
                pageData: playback
                backdrop: backdrop
            }
            Vinyl.VinylPlayerBar {
                id: hud
                width: hudWindow.width
                y: hudWindow.height - height
                pageData: playback
                page: hudWindow.page
                backdrop: backdrop
                videoFullscreen: hudWindow.videoFullscreen
                onVideoFullscreenRequested: hudWindow.videoFullscreen = !hudWindow.videoFullscreen
                onCreditsRequested: { credits.pageData = hud.songData; credits.open(); }
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
    function test_directCreditsUseViewedSong() {
        var actions=findChild(bar,"vinylBarSongActions");
        compare(item("vinylBarCredits").accessibleDescription,"For " + viewed.playingTrack.title);
        verify(item("vinylBarCredits").enabled);
        mouseClick(item("vinylBarCredits"));
        var popup=findChild(host,"vinylCreditsPopup");
        tryCompare(popup,"opened",true);
        compare(popup.pageData,viewed);
    }
    function test_firstViewedSongCanPlayWithoutAnExistingSession() {
        var empty=emptyPlaybackFactory.createObject(host.contentItem);
        bar.pageData=empty;
        verify(bar.playViewedWithoutSession);
        compare(bar.displayedSong,viewed);
        verify(item("vinylBarPlay").enabled);
        mouseClick(item("vinylBarPlay"));
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
        mouseClick(item("vinylSongNext"));
        mouseClick(item("vinylSongQueue"));
        compare(viewed.enqueues,2);
        mouseClick(item("vinylBarQueue"));
        compare(queueSpy.count,1);
        compare(bar.pageData,owner);
        compare(bar.currentSeekTrackKey,key);
    }
    function test_videoRequestsStayWithViewedSong() {
        var owner=bar.pageData;
        viewed.videoShown=true;
        mouseClick(item("vinylBarVideo"));
        verify(!viewed.videoShown);
        verify(!owner.videoShown);
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
    function test_albumRetainsTransportAndQueue() {
        bar.page="album";
        waitForRendering(host.contentItem);
        compare(bar.height,72);
        var play=item("vinylBarPlay");
        compare(play.mapToItem(bar,play.width/2,0).x,bar.width/2);
        verify(item("vinylBarQueue").visible);
        verify(!item("vinylSongSave").visible);
        verify(!item("vinylBarVideo").visible);
        mouseClick(item("vinylBarQueue"));
        compare(queueSpy.count,1);
    }
    function test_directActionsFitWithoutOverlap_data() {
        return [{tag:"desktop",width:1280},{tag:"actual",width:1028},
            {tag:"actual-current",width:1028,current:true},
            {tag:"medium",width:980},{tag:"narrow",width:546},{tag:"fallback",width:280},{tag:"tiny",width:200}];
    }
    function test_directActionsFitWithoutOverlap(data) {
        viewed.videoShown=true;
        bar.showSongPlay=data.current!==true;
        bar.width=data.width;
        waitForRendering(host.contentItem);
        var names=["vinylBarOpenSong","vinylBarNowPlaying","vinylBarPrev","vinylBarPlay",
            "vinylBarNext","vinylBarSeek","vinylBarElapsed","vinylBarDuration","vinylBarLyrics","vinylBarNotes","vinylBarQueue",
            "vinylSongPlay","vinylSongSave","vinylSongPlaylist","vinylSongNext","vinylSongQueue",
            "vinylBarVideo","vinylBarFullscreen","vinylBarCredits","vinylBarSourceIndicator"];
        var bounds=[];
        for(var i=0;i<names.length;i++) {
            var button=item(names[i]);
            verify(button!==null,names[i]);
            if(!button.visible)continue;
            var pos=button.mapToItem(bar,0,0);
            var end=button.mapToItem(bar,button.width,button.height);
            verify(pos.x>=0 && end.x<=bar.width+0.1,names[i]+" fits width");
            verify(pos.y>=0 && end.y<=bar.height+0.1,names[i]+" fits height");
            bounds.push({name:names[i],x:pos.x,y:pos.y,w:end.x-pos.x,h:end.y-pos.y});
        }
        for(var j=0;j<bounds.length;j++)for(var k=j+1;k<bounds.length;k++) {
            var a=bounds[j],b=bounds[k];
            verify(a.x+a.w<=b.x+0.1 || b.x+b.w<=a.x+0.1 || a.y+a.h<=b.y+0.1 || b.y+b.h<=a.y+0.1,
                a.name+" must not overlap "+b.name);
        }
        var play = item("vinylBarPlay"), seek = item("vinylBarSeek");
        compare(play.mapToItem(bar, play.width / 2, 0).x, bar.width / 2);
        compare(seek.mapToItem(bar, seek.width / 2, 0).x, bar.width / 2);
        var leftNames = ["vinylSongPlay", "vinylSongSave", "vinylSongPlaylist", "vinylSongNext", "vinylSongQueue", "vinylBarQueue"];
        var rightNames = ["vinylBarLyrics", "vinylBarNotes", "vinylBarVideo", "vinylBarFullscreen", "vinylBarCredits"];
        if(data.width >= 520) {
            for(var l=0;l<leftNames.length;l++) {
                var left=item(leftNames[l]);
                if(!left.visible)continue;
                verify(left.mapToItem(bar,left.width,0).x < bar.width / 2, leftNames[l]+" is left of transport");
            }
            for(var r=0;r<rightNames.length;r++)
                verify(item(rightNames[r]).mapToItem(bar,0,0).x > bar.width / 2, rightNames[r]+" is right of transport");
        }
        for(var n=0;n<names.length;n++) compare(countNamed(bar,names[n]),1,names[n]+" is unique");
        verify(seek.width>50,"Usable seek control");
        if(data.width>=900)verify(seek.width>=bar.width*0.8,"Timeline uses the available HUD width");
        var timelineY=seek.mapToItem(bar,0,0).y;
        for(var c=0;c<bounds.length;c++) {
            var bound=bounds[c];
            if(bound.name==="vinylBarSeek" || bound.name==="vinylBarElapsed" || bound.name==="vinylBarDuration")continue;
            verify(bound.y+bound.h<=timelineY-10,bound.name+" has clear space above the timeline");
        }
        compare(findChild(bar,"vinylBarSongActionsButton"),null);
        compare(item("vinylSongQueue").accessibleName,"Add to queue");
        compare(item("vinylBarQueue").accessibleName,"Open queue");
    }
    function test_queueAndPlaylistGlyphsAreDistinct() {
        var names=["vinylSongPlaylistGlyph","vinylSongNextGlyph","vinylSongQueueGlyph","vinylBarQueueGlyph","vinylBarVideoGlyph"];
        var seen={};
        for(var i=0;i<names.length;i++) {
            var glyph=item(names[i]);verify(glyph!==null,names[i]);
            verify(glyph.iconMarkup.length>0,names[i]+" has a visible design");
            verify(!seen[glyph.iconMarkup],names[i]+" must not reuse another action's glyph");
            seen[glyph.iconMarkup]=true;
        }
    }
}
