import QtQuick
import QtTest
import "../../qml/AudiobookSessionLogic.js" as L
TestCase {
    name: "AudiobookSessionLogic"
    function test_tick_data() { return [
        {tag:"countdown", now:0,remain:30,factor:1,pause:false},
        {tag:"countdown-later", now:15000,remain:15,factor:1,pause:false},
        {tag:"fade-10", now:20000,remain:10,factor:1,pause:false},
        {tag:"fade-5", now:25000,remain:5,factor:0.5,pause:false},
        {tag:"fade-0", now:30000,remain:0,factor:0,pause:true},
        {tag:"expired", now:31000,remain:0,factor:0,pause:true}
    ] }
    function test_tick(data) {
        var r=L.sleepTick({mode:15,deadlineMs:30000},data.now)
        compare(r.remainingSec,data.remain);compare(r.volumeFactor,data.factor);compare(r.shouldPause,data.pause)
    }
    function test_chapterEnd() {
        verify(!L.stopsAtChapterEnd("chapterEnd",0,0,false))
        verify(L.stopsAtChapterEnd("chapterEnd",0,1,false)) // file or embedded chapter index
        verify(L.stopsAtChapterEnd("chapterEnd",0,0,true))
        verify(!L.stopsAtChapterEnd("off",0,1,true))
        verify(!L.stopsAtChapterEnd(15,0,1,true))
    }
    function test_cancel() {var r=L.sleepTick({mode:"off",deadlineMs:1},9999);compare(r.volumeFactor,1);verify(!r.shouldPause);compare(r.remainingSec,0)}
    function test_labels() {compare(L.countdownLabel(30,1440),"Sleeps in 24 min");compare(L.countdownLabel("chapterEnd",0),"Sleeps at chapter end");compare(L.countdownLabel("off",0),"")}
    function test_speed() {compare(L.speedFromResume(null),1);compare(L.speedFromResume({speed:NaN}),1);var before={position:17,fileIndex:2};var after=L.withSpeed(before,1.5);compare(L.speedFromResume(after),1.5);compare(after.position,17);verify(before.speed===undefined);compare(L.speedFromResume({speed:0}),1)}
    function test_choices() {compare(L.choices().length,6);compare(L.choices()[5],"chapterEnd")}
}
