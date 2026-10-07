import QtQuick
import "VinylFixtures.js" as Fixtures

// A zero-size Item keeps this single owner readable by Lanista's QQuickItem-only resolver.
Item {
    id: root
    objectName: "vinylPageData"
    property url artworkRoot: ""
    property string fixtureId: "thriller"
    property string shownEditionId: "original"
    property int shownDisc: 0
    property var editions: Fixtures.editions(fixtureId)
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
    readonly property var videos: [
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
    readonly property var moreAlbums: [
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
        var candidates = [["released", "Released", e.date], ["label", "Label", e.label], ["catalogue", "Catalogue number", e.catalogue], ["barcode", "Barcode", e.barcode], ["country", "Country", e.country], ["format", "Format", e.format], ["tracks", "Tracks", totalTracks(e)], ["length", "Length", e.length]];
        return candidates.filter(function (row) {
            return row[2] !== undefined && row[2] !== "";
        });
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
    property bool lyricsShown: false
    property bool autoNotes: false
    property bool videoShown: false
    property int manualNoteLine: -1
    property int manualAtLine: -1
    readonly property int activeNoteLine: {
        if (manualNoteLine >= 0)
            return manualNoteLine;
        if (!autoNotes)
            return -1;
        for (var i = currentLineIndex; i >= 0; --i)
            if (notes[i] && notes[i].length)
                return i;
        return -1;
    }
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
    function playTrack(index) {
        if (index < 0 || index >= tracks.length)
            return;
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
        playingIndex = (playingIndex + delta + list.length) % list.length;
        position = 0;
        resetSongTools();
    }
    function seek(seconds) {
        position = Math.max(0, Math.min(duration, seconds));
    }
    function seekToLine(index) {
        if (timedLyrics && lyrics[index])
            seek(lyrics[index].time);
    }
    function setFixture(id) {
        if (["thriller", "without-me", "mounamela"].indexOf(id) < 0)
            return;
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
        manualNoteLine = -1;
        videoShown = false;
    }
    function toggleLyrics() {
        lyricsShown = !lyricsShown;
        if (!lyricsShown) {
            manualNoteLine = -1;
            autoNotes = false;
        }
    }
    function toggleNotes() {
        if (!hasNotes)
            return;
        autoNotes = !autoNotes;
        if (autoNotes)
            lyricsShown = true;
        manualNoteLine = -1;
    }
    function showNotes(index) {
        if (!notes[index] || !notes[index].length)
            return;
        lyricsShown = true;
        manualNoteLine = index;
        manualAtLine = currentLineIndex;
    }
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
    onCurrentLineIndexChanged: {
        if (autoNotes && currentLineIndex !== manualAtLine && notes[currentLineIndex])
            manualNoteLine = -1;
    }
    property Timer clock: Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: root.tick(0.1)
    }
}
