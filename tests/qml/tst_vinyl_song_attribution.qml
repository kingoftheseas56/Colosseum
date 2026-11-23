import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylSongAttribution"
    when: windowShown
    visible: true
    width: 1280
    height: 620
    property var data
    property var page

    Component {
        id: dataFactory
        QtObject {
            property var playingTrack: ({title: "An original song", artists: ["Original artist"]})
            property var playingEdition: ({id: "original", artist: "Original artist", cover: "", origin: "stream"})
            property var playingCredits: []
            property bool lyricsShown: true
            property bool autoNotes: false
            property bool videoShown: false
            property bool hasLyrics: true
            property bool hasNotes: true
            property bool timedLyrics: false
            property var lyrics: [{text: "An original opening"}, {text: "An original ending"}]
            property string lyricsStatus: "plain"
            property string lyricsSource: "lrclib"
            property string lyricsError: ""
            property string lyricsDocumentId: "document:one"
            property string readingContextKey: "song:one"
            property bool lyricsFollowing: true
            property real lyricsScrollY: 0
            property real notesScrollY: 0
            property int currentLineIndex: -1
            property int manualNoteLine: 0
            property int activeNoteLine: 0
            property real position: NaN
            property var notes: ({})
            property string annotationsStatus: "ready"
            property int annotationCount: 2
            property string geniusSongId: "42"
            function noteWindow(index) { return 120000; }
            function artwork(value) { return ""; }
            function toggleNotes() {
                autoNotes = manualNoteLine >= 0 || !autoNotes;
                manualNoteLine = -1;
                activeNoteLine = autoNotes && notes[currentLineIndex] ? currentLineIndex : -1;
            }
            function seekToLine(index) {
                position = lyrics[index].time;
                currentLineIndex = index;
                activeNoteLine = autoNotes && notes[index] ? index : -1;
            }
            function showNotes(index) {
                manualNoteLine = index;
                activeNoteLine = index;
            }
        }
    }
    Component { id: pageFactory; Vinyl.VinylSongPage {} }

    function note(id, authors, votes) {
        return {source: "genius", sourceUrl: "https://genius.com/" + id,
            text: "An original explanation.", authors: authors, votes: votes || 0};
    }
    function init() {
        data = dataFactory.createObject(test);
        data.notes = {0: [note("101", [{id: "1", name: "First", url: "https://genius.com/First"}], 20)]};
        page = pageFactory.createObject(test, {pageData: data, width: test.width, height: test.height});
        verify(page !== null);
        wait(0);
    }
    function cleanup() {
        page.destroy();
        wait(0);
        data.destroy();
        wait(0);
    }
    function rail() { return findChild(page, "vinylSongSources"); }

    function test_creditsUseDisplayedNotesAndDeduplicateAuthors() {
        data.notes = {0: [note("101", [{id: "1", name: "First"}], 20),
            note("102", [{id: "1", name: "First"}, {id: "2", name: "Second"}], 30)],
            1: [note("103", [{id: "3", name: "Other passage"}], 40)]};
        compare(rail().contributors.length, 2);
        compare(rail().contributors[0].name, "First");
        compare(rail().contributors[1].name, "Second");
        compare(rail().annotationSourceUrl, "https://genius.com/101");
        compare(rail().annotationSources.length, 2);
        compare(rail().annotationSources[1], "https://genius.com/102");
        compare(rail().lyricsSourceUrl, "https://lrclib.net");
        compare(findChild(page, "vinylLyricsSourceCredit").visible, false);
        compare(findChild(page, "vinylGeniusAttribution").visible, false);
        data.activeNoteLine = 1;
        compare(rail().contributors.length, 1);
        compare(rail().contributors[0].name, "Other passage");
        compare(rail().annotationSourceUrl, "https://genius.com/103");
    }
    function test_autoCreditsExcludeNotesThatAreNotShown() {
        data.notes = {0: [note("101", [{id: "1", name: "Visible author"}], 20),
            note("102", [{id: "2", name: "Filtered author"}], 1)]};
        data.manualNoteLine = -1;
        data.autoNotes = true;
        tryCompare(findChild(page,"vinylNotesColumn"),"noteCount",1);
        compare(rail().contributors.length, 1);
        compare(rail().contributors[0].name, "Visible author");
        compare(rail().annotationSourceUrl, "https://genius.com/101");
    }
    function test_underlinedLyricsOfferFullReadingWhenAutoCannotShowAnExcerpt() {
        data.timedLyrics = true;
        data.lyricsStatus = "line_synced";
        data.lyrics = [{text: "An original opening", time: 0}, {text: "An original ending", time: 2}];
        var ending = note("102", [{name: "Ending author"}], 1);
        ending.text = "The full annotation for the ending.";
        data.notes = {0: [note("101", [{name: "Opening author"}])], 1: [ending]};
        data.manualNoteLine = -1;
        data.currentLineIndex = 0;
        data.position = 0;
        data.activeNoteLine = -1;
        mouseClick(findChild(page, "vinylBarNotes"));
        verify(data.autoNotes);
        var column = findChild(page, "vinylNotesColumn");
        verify(column.visible && column.width > 0 && column.height > 0);
        compare(column.noteCount, 0, "Unqualified notes must not be presented as automatic excerpts");
        var open = findChild(page, "vinylAnnotationManualFallback");
        verify(open && open.visible && open.enabled, "An annotated cue must not leave the right pane blank");

        var line = findChild(page, "vinylLyricLine_1");
        verify(line.hasAnnotation);
        mouseClick(line);
        compare(data.position, 2);
        compare(data.manualNoteLine, -1, "A lyric seek must keep Auto following");
        verify(open.visible && open.enabled);
        compare(column.shownLineIndex, 1);
        waitForRendering(open);
        mouseClick(open);
        tryCompare(data, "manualNoteLine", 1);
        compare(column.shownLineIndex, 1);
        compare(column.noteCount, 1);
        var body = findChild(column, "vinylAnnotationBody_0");
        verify(body && body.visible);
        compare(body.text, ending.text);
        verify(!open.visible);
    }
    function test_unqualifiedSongMatchDoesNotProduceSourceLink() {
        data.activeNoteLine = -1;
        compare(rail().contributors.length, 0);
        compare(rail().annotationSourceUrl, "https://genius.com/songs/42");
        data.annotationsStatus = "ambiguous";
        compare(rail().annotationSourceUrl, "");
        data.annotationsStatus = "loading";
        compare(rail().annotationSourceUrl, "");
        data.annotationsStatus = "ready";
        data.geniusSongId = "42?other=1";
        compare(rail().annotationSourceUrl, "");
        data.geniusSongId = "42";
        data.annotationCount = 0;
        compare(rail().annotationSourceUrl, "");
    }
    function test_hiddenLyricsKeepPinAndClearDisplayedContributors() {
        compare(rail().contributors.length, 1);
        data.lyricsShown = false;
        compare(rail().visible, false);
        compare(rail().contributors.length, 0);
        compare(data.manualNoteLine, 0);
        data.lyricsShown = true;
        compare(rail().contributors.length, 1);
        data.videoShown = true;
        compare(rail().visible, false);
        compare(rail().contributors.length, 0);
    }
    function test_sourceRailFitsBelowArtworkAndAbovePageBottom() {
        var poster = findChild(page, "vinylSongPoster");
        var heights = [620, 500, 400, 320];
        data.playingTrack = {title: "An original song title spanning several lines", artists: ["Original artist"]};
        for (var i = 0; i < heights.length; ++i) {
            page.height = heights[i];
            wait(0);
            var top = rail().mapToItem(page, 0, 0).y;
            verify(top > poster.mapToItem(page, 0, poster.height).y);
            verify(top + rail().height <= page.height, "Source icons must stay above the player at " + heights[i]);
        }
    }
    function test_autoHoldKeepsItsOwnAttributionAcrossAnUnannotatedLine() {
        var column=findChild(page,"vinylNotesColumn");
        data.position=10;data.autoNotes=true;data.manualNoteLine=-1;
        tryCompare(column,"noteCount",1);
        compare(rail().contributors[0].name,"First");
        data.activeNoteLine=-1;data.position=11;
        verify(column.visible,"The reading interval must survive a gap in annotated lyrics");
        compare(column.shownLineIndex,0);
        compare(rail().contributors[0].name,"First");
        data.position=column.automaticUntilMs/1000+0.1;
        tryCompare(column,"visible",false);
        compare(rail().contributors.length,0);
    }
}
