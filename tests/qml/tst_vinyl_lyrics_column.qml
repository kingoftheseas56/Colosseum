import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylLyricsColumn"
    when: windowShown
    visible: true
    width: 500;height: 400
    property var column
    property var data
    Component {
        id: dataFactory
        QtObject {
            property var lyrics: []
            property var notes: ({})
            property int manualNoteLine: -1
            property bool autoNotes: false
            property int currentLineIndex: 3
            property bool timedLyrics: true
            property string lyricsStatus: "line_synced"
            property bool lyricsFollowing: true
            property real lyricsScrollY: 0
            property string readingContextKey: "profile:one/queue:one"
            property string lyricsDocumentId: "document:one"
            property string lyricsSource: ""
            property int seeks: 0
            function seekToLine(index) {seeks++;}
            function showNotes(index) {manualNoteLine=index;}
            function pauseLyrics(y) {lyricsFollowing=false;lyricsScrollY=y;}
            function resumeLyrics() {lyricsFollowing=true;}
        }
    }
    function init() {
        visible=true;data=dataFactory.createObject(test);
        data.lyrics=[{text:"An original opening",cueId:"one",time:0},
            {text:Array(35).join("A longer original line that must wrap in this narrow column. "),cueId:"two",time:1},
            {text:"An original bridge",cueId:"three",time:2},
            {text:"The current original line",cueId:"four",time:3},
            {text:"A final original line",cueId:"five",time:4}];
        var component=Qt.createComponent("../../qml/vinyl/VinylLyricsColumn.qml");
        compare(component.status,Component.Ready,component.errorString());
        column=component.createObject(test,{pageData:data,width:200,height:300});
        verify(column!==null);
        tryVerify(function(){return column.ready && column.contentHeight>column.height;});
        wait(0);
    }
    function cleanup() {visible=true;if(column)column.destroy();wait(0);data.destroy();wait(0);}
    function current() {return findChild(column,"vinylLyricLine_3");}
    function textFor(index) {return findChild(column,"vinylLyricText_"+index);}
    function waitForScrollToSettle() {
        var previous=-1,stable=0;
        tryVerify(function(){
            stable=Math.abs(column.contentY-previous)<0.1?stable+1:0;previous=column.contentY;
            return stable>=3;
        });
    }
    function test_followUsesWrappedDelegateGeometry() {
        var line=current(),scroll=findChild(column,"vinylLyricsScroll");
        verify(line.y>44*3+100,"The preceding line must actually wrap");
        tryVerify(function(){return Math.abs(line.mapToItem(scroll,0,0).y-scroll.height*0.34)<1;});
        column.width=260;
        tryVerify(function(){return Math.abs(line.mapToItem(scroll,0,0).y-scroll.height*0.34)<1;});
        verify(data.lyricsFollowing);
    }
    function test_keyboardCanPinWithoutSeekingAndReadPlainText() {
        data.notes={3:[{text:"An original source annotation."}]};
        var action=findChild(column,"vinylLyricAction_3");
        verify(action && action.enabled && action.contextEnabled);
        action.forceActiveFocus();
        tryVerify(function(){return action.activeFocus;});
        verify(!data.lyricsFollowing);
        keyClick(Qt.Key_Menu);
        compare(data.manualNoteLine,3);
        compare(data.seeks,0,"Explicitly reading a note must not seek");
        data.manualNoteLine=-1;data.timedLyrics=false;
        action.forceActiveFocus();keyClick(Qt.Key_Return);
        compare(data.manualNoteLine,3);compare(data.seeks,0);
    }
    function test_footerControlsNeverCoverTheLyricsViewport() {
        data.lyricsSource="lrclib";data.lyricsFollowing=false;
        wait(0);
        var scroll=findChild(column,"vinylLyricsScroll");
        var credit=findChild(column,"vinylLyricsSourceCredit");
        var resume=findChild(column,"vinylLyricsResume");
        verify(credit.visible && resume.visible);
        verify(credit.y>=scroll.y+scroll.height+4,"Source credit must sit below the reading viewport");
        verify(resume.y>=scroll.y+scroll.height+4,"Return control must not cover lyric text");
        scroll.contentY=scroll.contentHeight-scroll.height;
        var last=findChild(column,"vinylLyricLine_4");
        verify(last.mapToItem(scroll,0,last.height).y<=scroll.height+1);
    }
    function test_realWheelPausesFollowing() {
        var scroll=findChild(column,"vinylLyricsScroll");
        mouseWheel(scroll,100,120,0,120);
        tryVerify(function(){return !data.lyricsFollowing;});
        var previous=-1,stable=0;
        tryVerify(function(){
            stable=Math.abs(column.contentY-previous)<0.1?stable+1:0;previous=column.contentY;
            return stable>=3;
        });
        var y=column.contentY;data.currentLineIndex=4;wait(30);
        compare(column.contentY,y);compare(data.lyricsScrollY,y);
    }
    function test_cueTransitionMovesThroughIntermediatePositions() {
        var start=column.contentY,scroll=findChild(column,"vinylLyricsScroll");
        data.currentLineIndex=4;
        var line=findChild(column,"vinylLyricLine_4");
        var target=line.y+scroll.height*0.15-scroll.height*0.34;
        wait(60);
        verify(column.contentY>start && column.contentY<target,"Cue changes must move smoothly rather than jump");
        tryVerify(function(){return Math.abs(column.contentY-target)<1;});
        verify(data.lyricsFollowing);
    }
    function test_playbackCueAloneSetsBrightnessAndFollowing() {
        data.autoNotes=false;data.notes={};data.manualNoteLine=-1;
        var oldCue=textFor(3),newCue=textFor(4),newLine=findChild(column,"vinylLyricLine_4");
        var inactive=textFor(0),scroll=findChild(column,"vinylLyricsScroll");
        tryVerify(function(){return oldCue.color.r>inactive.color.r;});
        data.currentLineIndex=4;
        wait(320);
        verify(newCue.color.r>oldCue.color.r,"Brightness must transfer to the new playback cue");
        tryVerify(function(){return Math.abs(newLine.mapToItem(scroll,0,0).y-scroll.height*0.34)<1;});
        compare(data.seeks,0);compare(data.manualNoteLine,-1);verify(data.lyricsFollowing);
    }
    function test_pinnedOldNoteCannotHighlightOrResumePlaybackFollowing() {
        data.notes={1:[{text:"An original source annotation."}]};
        var scroll=findChild(column,"vinylLyricsScroll"),action=findChild(column,"vinylLyricAction_1");
        scroll.forceActiveFocus();keyClick(Qt.Key_PageDown);
        verify(!data.lyricsFollowing);
        action.forceActiveFocus();keyClick(Qt.Key_Menu);
        compare(data.manualNoteLine,1);verify(!data.lyricsFollowing);
        wait(320);
        var parkedY=column.contentY,inactive=textFor(0);
        tryVerify(function(){return textFor(3).color.r>inactive.color.r;});
        compare(textFor(1).color.r,inactive.color.r,"A pinned annotation must not brighten its lyric");
        data.currentLineIndex=4;wait(320);
        verify(textFor(4).color.r>inactive.color.r);compare(textFor(1).color.r,inactive.color.r);
        compare(column.contentY,parkedY,"A pinned note must not resume a deliberately parked viewport");
        verify(!data.lyricsFollowing);compare(data.seeks,0);
    }
    function test_manualInputCancelsTheCurrentTransition() {
        data.currentLineIndex=4;wait(50);
        var scroll=findChild(column,"vinylLyricsScroll");
        scroll.forceActiveFocus();keyClick(Qt.Key_Home);
        wait(400);compare(column.contentY,0);
        verify(!data.lyricsFollowing);compare(data.lyricsScrollY,0);
    }
    function test_documentReplacementCancelsTheCurrentTransition() {
        data.currentLineIndex=4;wait(50);
        data.lyricsScrollY=0;data.currentLineIndex=0;data.lyricsDocumentId="document:replacement";
        wait(400);compare(column.contentY,0);verify(data.lyricsFollowing);
    }
    function test_programmaticRepositionDoesNotBecomeManualInput() {
        var scroll=findChild(column,"vinylLyricsScroll");
        scroll.contentY=Math.max(0,scroll.contentY-25);
        wait(0);
        verify(data.lyricsFollowing,"A geometry correction is not a user scroll");
    }
    function test_reopeningWithAnotherDocumentKeepsFollowing() {
        var next=dataFactory.createObject(test);
        next.lyricsDocumentId="document:two";
        next.currentLineIndex=27;
        var lyrics=[];
        for(var i=0;i<90;i++)lyrics.push({text:"Original passage "+i,cueId:"next:"+i,time:i});
        next.lyrics=lyrics;
        column.visible=false;column.pageData=next;column.visible=true;
        wait(30);
        var line=findChild(column,"vinylLyricLine_27"),scroll=findChild(column,"vinylLyricsScroll");
        tryVerify(function(){return Math.abs(line.mapToItem(scroll,0,0).y-scroll.height*0.34)<1;});
        verify(next.lyricsFollowing);
        column.pageData=data;next.destroy();
    }
    function test_unknownClockDuringRetryKeepsViewportAndFollowingPreference() {
        var y=column.contentY;
        verify(y>0);data.timedLyrics=false;data.currentLineIndex=-1;
        wait(0);compare(column.contentY,y);verify(data.lyricsFollowing);
        data.currentLineIndex=4;data.timedLyrics=true;
        var line=findChild(column,"vinylLyricLine_4"),scroll=findChild(column,"vinylLyricsScroll");
        tryVerify(function(){return Math.abs(line.mapToItem(scroll,0,0).y-scroll.height*0.34)<1;});
        verify(data.lyricsFollowing);
    }
    function test_oneClickImmediatelySeeksAndSelectsNotes() {
        data.notes={3:[{text:"An original source annotation."}]};
        mouseClick(current());
        compare(data.seeks,1);compare(data.manualNoteLine,3);
    }
    function test_pointerActivationResumesAfterBrowsingAndLaterScrollStaysParked() {
        var scroll=findChild(column,"vinylLyricsScroll");
        mouseWheel(scroll,100,120,0,-120);
        tryVerify(function(){return !data.lyricsFollowing;});
        mouseClick(current());
        compare(data.seeks,1);tryVerify(function(){return data.lyricsFollowing;});
        wait(320);
        mouseWheel(scroll,100,120,0,120);
        tryVerify(function(){return !data.lyricsFollowing;});
        waitForScrollToSettle();
        var parkedY=column.contentY;
        data.currentLineIndex=4;wait(320);
        compare(column.contentY,parkedY);verify(!data.lyricsFollowing);
        verify(textFor(4).color.r>textFor(3).color.r,"Playback brightness must advance while the viewport remains parked");
        compare(data.seeks,1);
    }
    function test_enterActivationResumesAfterBrowsing() {
        var scroll=findChild(column,"vinylLyricsScroll"),action=findChild(column,"vinylLyricAction_4");
        mouseWheel(scroll,100,120,0,-120);
        tryVerify(function(){return !data.lyricsFollowing;});
        action.forceActiveFocus();keyClick(Qt.Key_Return);
        compare(data.seeks,1);tryVerify(function(){return data.lyricsFollowing;});
    }
    function test_autoClickKeepsAutomaticFollowing() {
        data.autoNotes=true;
        mouseClick(current());
        compare(data.seeks,1);compare(data.manualNoteLine,-1);verify(data.autoNotes);
    }
    function test_anotherClickReplacesTheOldManualNote() {
        data.autoNotes=true;data.manualNoteLine=1;
        data.notes={1:[{text:"Old annotation"}]};
        mouseClick(current());
        compare(data.seeks,1);compare(data.manualNoteLine,3);
        verify(!data.notes[data.manualNoteLine],"An unannotated selection must clear the unrelated note");
    }
    function test_wheelAfterClickDoesNotRepeatTheSeek() {
        mouseClick(current());
        compare(data.seeks,1);
        mouseWheel(findChild(column,"vinylLyricsScroll"),100,120,0,120);
        wait(Application.styleHints.mouseDoubleClickInterval+50);
        compare(data.seeks,1);verify(!data.lyricsFollowing);
    }
    function test_hidingAfterClickCannotCauseAnotherSeek() {
        mouseClick(current());test.visible=false;
        wait(Application.styleHints.mouseDoubleClickInterval+50);compare(data.seeks,1);
    }
    function test_hideAndShowKeepsDeliberateScrollPaused() {
        var scroll=findChild(column,"vinylLyricsScroll");
        mouseWheel(scroll,100,120,0,120);
        tryVerify(function(){return !data.lyricsFollowing;});
        var parkedY=column.contentY;
        column.visible=false;wait(0);column.visible=true;wait(50);
        verify(!data.lyricsFollowing);compare(column.contentY,parkedY);
    }
    function test_documentOrProfileChangeCannotCauseAnotherSeek() {
        mouseClick(current());data.lyricsDocumentId="document:two";
        wait(Application.styleHints.mouseDoubleClickInterval+50);compare(data.seeks,1);
        mouseClick(current());data.readingContextKey="profile:two/queue:one";
        wait(Application.styleHints.mouseDoubleClickInterval+50);compare(data.seeks,2);
    }
}
