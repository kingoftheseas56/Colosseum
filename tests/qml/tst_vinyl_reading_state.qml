import QtQuick
import QtTest
import "../../qml/vinyl"

TestCase {
    name: "VinylReadingState"
    VinylReadingState { id: state }
    function init() {
        state.songKey="song:one";state.documentId="document:one";
        state.clear();
    }
    function test_manualReadingSurvivesVisibilityAndPlayback() {
        state.toggleLyrics(true);state.autoNotes=true;
        state.pauseLyrics(180);state.showNotes(3);state.notesScrollY=75;
        state.toggleLyrics(true);
        compare(state.lyricsShown,false);compare(state.manualNoteLine,3);
        compare(state.lyricsScrollY,180);compare(state.notesScrollY,75);
        state.toggleLyrics(true);
        compare(state.lyricsFollowing,false);compare(state.autoNotes,true);
        compare(state.manualNoteLine,3);
        state.resumeLyrics();compare(state.lyricsFollowing,true);
        compare(state.manualNoteLine,3,"Returning to the current lyric must retain the note pin");
    }
    function test_annotationsButtonResumesAutoThenTogglesOff() {
        state.autoNotes=true;state.showNotes(2);state.pauseLyrics(120);
        state.toggleNotes(true);
        compare(state.manualNoteLine,-1);compare(state.autoNotes,true);
        compare(state.lyricsFollowing,false);
        state.toggleNotes(true);compare(state.autoNotes,false);
        state.toggleNotes(false);compare(state.autoNotes,false);
    }
    function test_newOccurrenceOrDocumentResetsReadingOnly() {
        state.lyricsShown=true;state.autoNotes=true;state.pauseLyrics(90);state.showNotes(4);
        state.songKey="song:two";
        compare(state.lyricsFollowing,true);compare(state.lyricsScrollY,0);compare(state.manualNoteLine,-1);
        compare(state.lyricsShown,true);compare(state.autoNotes,true);
        state.pauseLyrics(75);state.showNotes(2);state.documentId="document:two";
        compare(state.lyricsFollowing,true);compare(state.lyricsScrollY,0);compare(state.manualNoteLine,-1);
        compare(state.lyricsShown,true);compare(state.autoNotes,true);
    }
    function test_autoActionRevealsHiddenLyricsAndResumes() {
        state.lyricsShown=false;state.autoNotes=true;
        state.toggleNotes(true);
        verify(state.lyricsShown);verify(state.autoNotes);compare(state.manualNoteLine,-1);
        state.toggleNotes(true);verify(!state.autoNotes);
    }
}
