import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylLyricsPlaybackFollow"
    width: 720
    height: 520
    visible: true
    when: windowShown
    property var session
    property var column
    SignalSpy { id: seekSpy; signalName: "discontinuity" }

    function init() {
        var imported=NativeSource.importFiles([ManifestPath]);
        compare(imported.errors.length,0);
        var album=JSON.parse(JSON.stringify(imported.albums[0]));
        var cues=[];
        for(var i=0;i<5;i++)cues.push({text:"Original timed passage "+i+" continuing across this narrow reading column.",time:i*0.4});
        album.tracks[0].lyrics=cues;
        (album.editions || []).forEach(function(edition){edition.tracks[0].lyrics=cues;});
        NativeSource.setAlbums([album]);
        var sessionComponent=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(sessionComponent.status,Component.Ready,sessionComponent.errorString());
        session=sessionComponent.createObject(test,{source:NativeSource,contentProvider:null,collectionStore:null,progressStore:null});
        verify(session!==null);
        session.shownEdition=session.normalize(album);
        session.playTrack(0);
        tryVerify(function(){return session.loaded;});
        session.paused=true;
        tryCompare(session,"timedLyrics",true);
        session.toggleLyrics();
        var columnComponent=Qt.createComponent("../../qml/vinyl/VinylLyricsColumn.qml");
        compare(columnComponent.status,Component.Ready,columnComponent.errorString());
        column=columnComponent.createObject(test,{pageData:session,width:220,height:160});
        verify(column!==null);
        seekSpy.target=findChild(session,"vinylBackendSession");
        verify(seekSpy.valid);
        seekSpy.clear();
    }
    function cleanup() {
        if(column)column.destroy();
        if(session)session.destroy();
        column=null;session=null;
        wait(0);
    }
    function test_realPlaybackHighlightsAndScrollsWithoutAnnotationsOrClicks() {
        compare(session.hasNotes,false);
        compare(session.autoNotes,false);
        compare(session.manualNoteLine,-1);
        verify(column.following);
        var before=column.contentY;
        session.paused=false;
        tryVerify(function(){return session.currentLineIndex>=1;});
        session.paused=true;
        var first=session.currentLineIndex;
        verify(findChild(column,"vinylLyricLine_"+first).isPlayingCue);
        tryVerify(function(){return column.contentY>before+1;});
        var firstY=column.contentY;
        session.paused=false;
        tryVerify(function(){return session.currentLineIndex>first;});
        session.paused=true;
        var second=session.currentLineIndex;
        verify(findChild(column,"vinylLyricLine_"+second).isPlayingCue);
        verify(!findChild(column,"vinylLyricLine_"+first).isPlayingCue);
        tryVerify(function(){return column.contentY>firstY+1;});
        compare(session.manualNoteLine,-1);
        compare(session.hasNotes,false);
        compare(seekSpy.count,0,"Following real playback must not need lyric activation or a seek");
    }
}
