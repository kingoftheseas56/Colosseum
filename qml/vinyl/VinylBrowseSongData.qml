import QtQuick

// A selected-song view only: never owns a player, queue, history or durable state.
Item {
    id: root
    required property var session
    property var album: ({})
    property int trackIndex: 0
    property string editionId: ""
    readonly property var edition: (album.editions || []).find(function(e){return e.id===editionId}) || (album.editions || [])[0] || album
    readonly property var rawTrack: (edition.tracks || [])[trackIndex] || ({})
    readonly property var playingEdition: session.normalize(album,edition)
    readonly property string playingEditionId: playingEdition.id || ""
    readonly property var playingTrack: Object.assign({title:"",seconds:0},rawTrack)
    readonly property int playingIndex: trackIndex
    readonly property int discIndex: {var offset=0;for(var i=0;i<playingEdition.discs.length;i++){if(trackIndex<offset+playingEdition.discs[i].length)return i;offset+=playingEdition.discs[i].length;}return -1;}
    readonly property int indexOnDisc: {var offset=0;for(var i=0;i<discIndex;i++)offset+=playingEdition.discs[i].length;return trackIndex-offset;}
    readonly property var personalRow: session && typeof session.rowFor === "function" && discIndex >= 0 && indexOnDisc >= 0 ? session.rowFor(playingEdition,discIndex,indexOnDisc) : null
    readonly property bool personalEnabled: !!(session && session.personalEnabled)
    readonly property bool songIsSaved: personalEnabled && personalRow !== null && personalRow !== undefined && typeof session.songSaved === "function" ? !!session.songSaved(personalRow) : false
    readonly property bool isCurrent: !!rawTrack.id && session.playingTrack.id===rawTrack.id && session.playingEditionId===playingEditionId && session.playingEdition.albumId===album.id && session.playingDisc===discIndex && session.playingIndex===indexOnDisc
    readonly property var lyrics: session.lyricLines(rawTrack.lyrics)
    readonly property var notes: session.noteGroups(rawTrack.notes || album.notes)
    readonly property var credits: session.creditGroups(rawTrack.credits || album.credits || [])
    readonly property var playingCredits: credits
    readonly property bool hasLyrics: lyrics.length>0
    readonly property bool hasNotes: Object.keys(notes).length>0
    readonly property bool timedLyrics: lyrics.length>0 && lyrics.every(function(line){return typeof line.time==="number"})
    readonly property int currentLineIndex: {
        if(!isCurrent || !timedLyrics) return -1;
        var found=-1;for(var i=0;i<lyrics.length;i++) if(Number(lyrics[i].time!==undefined ? lyrics[i].time : lyrics[i].seconds)<=session.position) found=i;return found;
    }
    property bool lyricsShown: false
    property bool autoNotes: false
    property bool videoShown: false
    property int manualNoteLine: -1
    readonly property int activeNoteLine: manualNoteLine>=0 ? manualNoteLine : autoNotes && hasNotes ? Math.max(0,currentLineIndex) : -1
    function artwork(url){return url || "";}
    function play(){session.playAlbum(album,trackIndex,playingEditionId);}
    function enqueuePlayingSong(playNext){return session.enqueueSong(album,trackIndex,playingEditionId,!!playNext);}
    function togglePlayingSongSaved(){if(personalEnabled && personalRow!==null && personalRow!==undefined && typeof session.toggleSongSaved==="function")return session.toggleSongSaved(personalRow);return false;}
    function addPlayingSongToPlaylist(){if(personalEnabled && personalRow!==null && personalRow!==undefined && typeof session.addRowsToPlaylist==="function")return session.addRowsToPlaylist([personalRow]);return false;}
    function seekToLine(index){if(isCurrent && timedLyrics && lyrics[index]) session.seek(Number(lyrics[index].time!==undefined ? lyrics[index].time : lyrics[index].seconds));}
    function showNotes(index){manualNoteLine=index;}
    function noteWindow(index){return 12;}
    onRawTrackChanged:{manualNoteLine=-1;lyricsShown=false;}
}
