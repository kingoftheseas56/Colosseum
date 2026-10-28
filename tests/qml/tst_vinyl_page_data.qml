import QtQuick
import QtTest

TestCase {
    name: "VinylPageData"
    property var subject
    function init() {
        var c = Qt.createComponent("../../qml/vinyl/VinylPageData.qml");
        verify(c.status === Component.Ready, c.errorString());
        subject = c.createObject(this, {
            frozen: true
        });
        verify(subject !== null);
    }
    function cleanup() {
        if (subject)
            subject.destroy();
    }
    function test_edition() {
        compare(subject.tracks.length, 9);
        var cover = subject.shownEdition.cover;
        subject.selectEdition("40");
        compare(subject.shownEditionId, "40");
        verify(subject.shownEdition.cover !== cover);
        compare(subject.shownEdition.year, 2022);
        subject.selectDisc(1);
        compare(subject.tracks.length, 10);
    }
    function test_playingEditionIdentity() {
        subject.playTrack(2);
        compare(subject.playingIndex, 2);
        compare(subject.playingEditionId, "original");
        verify(subject.isPlayingTrack(2));
        subject.selectEdition("40");
        verify(!subject.isPlayingTrack(2));
        compare(subject.playingEditionId, "original");
        compare(subject.playingIndex, 2);
    }
    function test_artistCredit() {
        var count = 0;
        for (var i = 0; i < subject.tracks.length; i++)
            if (subject.trackArtists(i))
                count++;
        compare(count, 1);
    }
    function test_lyricSeek() {
        subject.setFixture("without-me");
        subject.seekToLine(3);
        compare(subject.position, subject.lyrics[3].time);
        compare(subject.currentLineIndex, 3);
    }
    function test_sourceHold() {
        subject.frozen = false;
        subject.paused = false;
        subject.position = 20;
        subject.setSourceState("retrying");
        subject.tick(1);
        compare(subject.position, 20);
        subject.setSourceState("playing");
        subject.tick(1);
        compare(subject.position, 21);
    }
}
