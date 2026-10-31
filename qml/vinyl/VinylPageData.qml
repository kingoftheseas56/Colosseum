import QtQuick
import "VinylFixtures.js" as Fixtures

// A zero-size Item keeps this single owner readable by Lanista's QQuickItem-only resolver.
Item {
    id: root
    objectName: "vinylPageData"
    signal playbackDiscontinuity(real seconds)
    property url artworkRoot: ""
    property string fixtureId: "thriller"
    property string shownEditionId: "original"
    property int shownDisc: 0
    property var snapshotEdition: null
    property var continuousQueue: []
    property int continuousIndex: 0
    readonly property string shownAlbumId: shownEdition.albumId || shownEdition.id
    property var editions: snapshotEdition ? [snapshotEdition] : Fixtures.editions(fixtureId)
    readonly property var shownEdition: edition(shownEditionId)
    readonly property var tracks: shownEdition.discs[shownDisc] || []
    property string playingEditionId: "original"
    property var playingEdition: Fixtures.thriller[0]
    property int playingDisc: 0
    property int playingIndex: 5
    readonly property var playingTrack: (playingEdition.discs[playingDisc] || playingEdition.discs[0])[playingIndex] || playingEdition.discs[0][0]
    readonly property real duration: playingTrack.seconds
    property real position: 96
    property bool paused: false
    property bool frozen: false
    property bool saved: false
    property string sourceState: "playing"
    property bool timedLyrics: true
    readonly property var lyrics: Fixtures.lines
    readonly property var notes: playingEdition.hasNotes ? Fixtures.notes : ({})
    readonly property bool hasLyrics: playingEdition.hasLyrics !== false
    readonly property bool hasNotes: !!playingEdition.hasNotes
    readonly property bool hasVideo: !!playingEdition.hasVideo || (playingEdition.artist === "Michael Jackson" && ["Thriller", "Beat It", "Billie Jean"].indexOf(playingTrack.title) >= 0)
    readonly property var credits: shownEdition.credits || Fixtures.credits
    readonly property var playingCredits: playingEdition.credits || Fixtures.credits
    readonly property var videos: shownEdition.snapshot ? [] : [
        {
            title: "Billie Jean",
            cover: "video-billie-jean.jpg",
            track: 5
        },
        {
            title: "Beat It",
            cover: "video-beat-it.jpg",
            track: 4
        },
        {
            title: "Thriller",
            cover: "video-thriller.jpg",
            track: 3
        }
    ]
    readonly property var moreAlbums: shownEdition.snapshot ? [] : [
        {
            title: "Off the Wall",
            year: 1979,
            cover: "off-the-wall.jpg"
        },
        {
            title: "Bad",
            year: 1987,
            cover: "bad.jpg"
        },
        {
            title: "Dangerous",
            year: 1991,
            cover: "dangerous.jpg"
        },
        {
            title: "HIStory",
            year: 1995,
            cover: "history.jpg"
        },
        {
            title: "Invincible",
            year: 2001,
            cover: "invincible.jpg"
        }
    ]
    readonly property var information: {
        var e = shownEdition;
        if (e.snapshot) return [];
        var candidates = [["released", "Released", e.date], ["label", "Label", e.label], ["catalogue", "Catalogue number", e.catalogue], ["barcode", "Barcode", e.barcode], ["country", "Country", e.country], ["format", "Format", e.format], ["tracks", "Tracks", totalTracks(e)], ["length", "Length", e.length]];
        return candidates.filter(function (row) {
            return row[2] !== undefined && row[2] !== "";
        });
    }
    function sectionVisible(key) {
        var visibility = shownEdition.sectionVisibility;
        return visibility ? visibility[key] === true : true;
    }
    function snapshotRecord(album, coverUrl) {
        var total = album.tracks.reduce(function(n,t) {return n + t.seconds},0);
        return {id:album.id,albumId:album.id,snapshot:true,name:album.title,title:album.title,
            artist:album.artists.join(" · "),year:album.year,cover:coverUrl,
            length:formatTime(total),about:"",credits:[],hasLyrics:false,hasNotes:false,hasVideo:false,
            sectionVisibility:{about:false,credits:false,videos:false,editions:false,more:false,information:false},
            discs:[album.tracks.map(function(t) {return {title:t.title,seconds:t.seconds,
                artist:(t.artists.length?t.artists:album.artists).join(" · ")}})]};
    }
    function loadAlbum(album, coverUrl) {
        if (!album || !album.tracks || !album.tracks.length) return false;
        snapshotEdition=snapshotRecord(album,coverUrl);
        shownDisc=0;shownEditionId=album.id;saved=false;editionSelected();return true;
    }
    function showPlayingAlbum() {
        snapshotEdition=playingEdition.snapshot ? playingEdition : null;
        shownEditionId=playingEditionId;
        shownDisc=playingDisc;
        editionSelected();
    }
    function startContinuous(records) {
        if (!records || !records.length) return;
        continuousQueue=records;continuousIndex=0;
        loadAlbum(records[0].album,records[0].cover);
        playTrack(0,true);
    }
    function totalTracks(e) {
        return e.discs.reduce(function (n, d) {
            return n + d.length;
        }, 0);
    }
    function toggleSaved() {
        saved = !saved;
    }
    function shuffle() {
        playTrack(Math.floor(Math.random() * tracks.length));
    }
    readonly property int currentLineIndex: {
        if (!timedLyrics)
            return -1;
        var found = -1;
        for (var i = 0; i < lyrics.length; ++i)
            if (lyrics[i].time <= position)
                found = i;
        return found;
    }
    readonly property string readingContextKey: JSON.stringify([fixtureId,playingEditionId,playingDisc,playingIndex])
    readonly property string lyricsDocumentId: "fixture:"+fixtureId
    VinylReadingState {
        id: reading
        songKey: root.readingContextKey
        documentId: root.lyricsDocumentId
    }
    property alias lyricsShown: reading.lyricsShown
    property alias autoNotes: reading.autoNotes
    property alias lyricsFollowing: reading.lyricsFollowing
    property alias lyricsScrollY: reading.lyricsScrollY
    property alias notesScrollY: reading.notesScrollY
    property bool videoShown: false
    property alias manualNoteLine: reading.manualNoteLine
    readonly property int activeNoteLine: {
        if (manualNoteLine >= 0)
            return notes[manualNoteLine] && notes[manualNoteLine].length ? manualNoteLine : -1;
        if (!autoNotes)
            return -1;
        for (var i = currentLineIndex; i >= 0; --i)
            if (notes[i] && notes[i].length)
                return i;
        return -1;
    }
    onActiveNoteLineChanged: notesScrollY=0
    signal editionSelected
    signal artistRequested(string name)
    signal queueRequested
    function edition(id) {
        for (var i = 0; i < editions.length; ++i)
            if (editions[i].id === id)
                return editions[i];
        return editions[0];
    }
    function artwork(file) {
        if (file && (/^(file:|qrc:|https?:)/).test(String(file))) return file;
        return file && artworkRoot ? artworkRoot + "/" + file : "";
    }
    function formatTime(seconds) {
        return Math.floor(seconds / 60) + ":" + ("0" + Math.floor(seconds % 60)).slice(-2);
    }
    function selectEdition(id) {
        if (!editions.some(function (e) {
            return e.id === id;
        }))
            return;
        shownDisc = 0;
        shownEditionId = id;
        editionSelected();
    }
    function selectDisc(index) {
        if (index >= 0 && index < shownEdition.discs.length)
            shownDisc = index;
    }
    function isPlayingTrack(index) {
        return playingEditionId === shownEditionId && playingDisc === shownDisc && playingIndex === index;
    }
    function trackArtists(index) {
        var t = tracks[index];
        return t && t.artist !== shownEdition.artist ? t.artist : "";
    }
    function playTrack(index, retainContinuous) {
        if (index < 0 || index >= tracks.length)
            return;
        if (retainContinuous !== true) continuousQueue=[];
        playingEdition = shownEdition;
        playingEditionId = shownEditionId;
        playingDisc = shownDisc;
        playingIndex = index;
        position = 0;
        paused = false;
        sourceState = "playing";
        resetSongTools();
    }
    function next(delta) {
        var list = playingEdition.discs[playingDisc];
        if (continuousQueue.length && delta > 0 && playingIndex === list.length-1) {
            continuousIndex=(continuousIndex+1)%continuousQueue.length;
            var record=continuousQueue[continuousIndex];
            playingEdition=snapshotRecord(record.album,record.cover);
            playingEditionId=playingEdition.id;playingDisc=0;playingIndex=0;
            position=0;resetSongTools();return;
        }
        playingIndex = (playingIndex + delta + list.length) % list.length;
        position = 0;
        resetSongTools();
    }
    function seek(seconds) {
        position = Math.max(0, Math.min(duration, seconds));
        playbackDiscontinuity(position);
    }
    function seekToLine(index) {
        if (timedLyrics && lyrics[index])
            seek(lyrics[index].time);
    }
    function setFixture(id) {
        if (["thriller", "without-me", "mounamela"].indexOf(id) < 0)
            return;
        snapshotEdition=null;
        continuousQueue=[];
        fixtureId = id;
        shownDisc = 0;
        shownEditionId = editions[0].id;
        playTrack(id === "thriller" ? 5 : 0);
        editionSelected();
    }
    function setSourceState(state) {
        if (["playing", "finding", "retrying"].indexOf(state) >= 0)
            sourceState = state;
    }
    function tick(seconds) {
        if (!frozen && !paused && sourceState === "playing") {
            if (position + seconds >= duration)
                next(1);
            else
                position += seconds;
        }
    }
    function resetSongTools() {
        reading.resetReading();
        videoShown = false;
    }
    function toggleLyrics() {
        reading.toggleLyrics(hasLyrics);
    }
    function toggleNotes() {
        reading.toggleNotes(hasNotes);
    }
    function showNotes(index) {
        if (!notes[index] || !notes[index].length)
            return;
        reading.showNotes(index);
    }
    function selectNoteLine(index) {
        if(Number.isInteger(index) && index>=0 && index<lyrics.length) reading.showNotes(index);
    }
    function pauseLyrics(y) { reading.pauseLyrics(y); }
    function resumeLyrics() { reading.resumeLyrics(); }
    function toggleVideo() {
        if (hasVideo)
            videoShown = !videoShown;
    }
    function noteWindow(index) {
        if (index < 0 || !lyrics[index])
            return 0;
        for (var i = index + 1; i < lyrics.length; ++i)
            if (notes[i] && notes[i].length)
                return (lyrics[i].time - lyrics[index].time) * 1000;
        return (duration - (lyrics[index] ? lyrics[index].time : 0)) * 1000;
    }
    property Timer clock: Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: root.tick(0.1)
    }
}
