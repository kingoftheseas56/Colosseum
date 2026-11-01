import QtQuick

// Session-scoped interaction state. Visibility never discards a reader's place.
QtObject {
    property string songKey: ""
    property string documentId: ""
    property bool lyricsShown: false
    property bool autoNotes: false
    property bool lyricsFollowing: true
    property real lyricsScrollY: 0
    property real notesScrollY: 0
    property int manualNoteLine: -1
    onSongKeyChanged: resetReading()
    onDocumentIdChanged: resetReading()
    onManualNoteLineChanged: notesScrollY=0
    function resetReading() {
        lyricsFollowing=true;lyricsScrollY=0;manualNoteLine=-1;notesScrollY=0;
    }
    function clear() {resetReading();lyricsShown=false;autoNotes=false;}
    function pauseLyrics(y) {
        lyricsFollowing=false;
        lyricsScrollY=isFinite(y)?Math.max(0,y):0;
    }
    function resumeLyrics() {lyricsFollowing=true;}
    function toggleLyrics(available) {if(available)lyricsShown=!lyricsShown;}
    function toggleNotes(available) {
        if(!available)return;
        if(manualNoteLine>=0 || !lyricsShown){manualNoteLine=-1;autoNotes=true;}
        else autoNotes=!autoNotes;
        if(autoNotes)lyricsShown=true;
    }
    function showNotes(index) {manualNoteLine=index;notesScrollY=0;lyricsShown=true;}
}
