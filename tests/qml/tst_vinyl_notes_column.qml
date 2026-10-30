import QtQuick
import QtTest

TestCase {
    name: "VinylNotesColumn"
    when: windowShown
    visible: true
    width: 400
    height: 240

    property var pageData
    property var notesColumn

    Component {
        id: pageDataFactory
        QtObject {
            property var notes: ({})
            property int activeNoteLine: 0
            property bool autoNotes: false
            property int manualNoteLine: 0
            property var playingEdition: ({ origin: "stream" })
            property real notesScrollY: 0
            property real position: 0
            property real availableMs: 120000
            property string readingContextKey: "song:one"
            property string lyricsDocumentId: "document:one"
            signal playbackDiscontinuity(real seconds)
            function noteWindow(index) { return availableMs; }
            function showNotes(index) {manualNoteLine=index;activeNoteLine=index;}
        }
    }

    Component {
        id: legacyPageDataFactory
        QtObject {
            property var notes: ({})
            property int activeNoteLine: 0
            property bool autoNotes: false
            property int manualNoteLine: 0
            property var playingEdition: ({ origin: "stream" })
            function noteWindow(index) { return 120000; }
        }
    }

    function longText(label) {
        var words = [];
        for (var i = 0; i < 260; ++i)
            words.push(label + " passage " + i + " remains readable.");
        return words.join(" ");
    }

    function makePageData(withSharedScroll) {
        var notes = {};
        notes[0] = [{ text: longText("First"), verified: true, votes: 20 }];
        notes[1] = [{ text: longText("Second"), verified: true, votes: 20 }];
        var properties = {
            notes: notes,
            activeNoteLine: 0,
            autoNotes: false,
            manualNoteLine: 0,
            playingEdition: { origin: "stream" }
        };
        if (withSharedScroll)
            properties.notesScrollY = 0;
        return pageDataFactory.createObject(this, properties);
    }

    function createNotesColumn(data) {
        var component = Qt.createComponent("../../qml/vinyl/VinylNotesColumn.qml");
        verify(component.status === Component.Ready, component.errorString());
        var column = component.createObject(this, {
            pageData: data,
            width: 250,
            height: 170
        });
        verify(column !== null);
        return column;
    }

    function destroyNotesColumn() {
        if (notesColumn) {
            notesColumn.destroy();
            notesColumn = null;
        }
    }

    function init() {
        pageData = makePageData(true);
        notesColumn = createNotesColumn(pageData);
        tryVerify(function() {
            var scroll = findChild(notesColumn, "vinylNotesScroll");
            return scroll && scroll.contentHeight > scroll.height;
        });
        tryVerify(function() { return notesColumn.initialRestoreComplete; });
    }

    function cleanup() {
        destroyNotesColumn();
        if (pageData) {
            pageData.destroy();
            pageData = null;
        }
    }

    function scrollView() {
        return findChild(notesColumn, "vinylNotesScroll");
    }

    function test_longManualAnnotationOverflowsAndCanBeRead() {
        var scroll = scrollView();
        verify(scroll !== null);
        verify(!notesColumn.autoMode);
        verify(scroll.contentHeight > scroll.height);
        scroll.forceActiveFocus();
        tryVerify(function() { return scroll.activeFocus; });
        keyClick(Qt.Key_PageDown);
        tryVerify(function() { return scroll.contentY > 0; });
        keyClick(Qt.Key_End);
        tryVerify(function() { return scroll.atYEnd; });
    }

    function test_geniusAttributionIsPlainKeyboardAccessibleAndScrollable() {
        var scroll = scrollView();
        var initialHeight = scroll.contentHeight;
        var note = {
            text: longText("First"),
            source: "genius",
            sourceUrl: "https://www.genius.com/annotations/attribution-check",
            authors: [{ name: "Writer <Actual Name>", url: "https://genius.com/authors/writer" }]
        };
        pageData.notes = { 0: [note] };

        var attribution = findChild(notesColumn, "vinylGeniusAttribution");
        var authorText = findChild(notesColumn, "vinylGeniusAuthors");
        var link = findChild(notesColumn, "vinylGeniusLink");
        tryVerify(function() { return attribution && attribution.visible && scroll.contentHeight > initialHeight; });
        compare(authorText.text, "Writer <Actual Name>");
        verify(link.visible && link.enabled);
        verify(link.activeFocusOnTab);
        compare(link.Accessible.role, Accessible.Link);
        link.forceActiveFocus();
        tryVerify(function() { return link.activeFocus; });

        scroll.forceActiveFocus();
        tryVerify(function() { return scroll.activeFocus; });
        keyClick(Qt.Key_End);
        tryVerify(function() { return scroll.atYEnd && pageData.notesScrollY > 0; });
        var saved = pageData.notesScrollY;
        notesColumn.visible = false;
        notesColumn.visible = true;
        tryCompare(scroll, "contentY", saved);

        var invalidUrls = [
            "http://genius.com/annotations/one",
            "https://genius.com.evil.example/annotations/one",
            "https://example.com/annotations/one",
            "https://genius.com\\@evil.example/annotations/one"
        ];
        for (var i = 0; i < invalidUrls.length; ++i) {
            pageData.notes = { 0: [{
                text: note.text,
                source: "genius",
                sourceUrl: invalidUrls[i],
                authors: note.authors
            }] };
            tryVerify(function() {
                var invalidLink = findChild(notesColumn, "vinylGeniusLink");
                return !invalidLink || (!invalidLink.visible && !invalidLink.enabled);
            });
        }
        compare(authorText.text, "Writer <Actual Name>");
    }

    function test_localFileGeniusNotesRemainEligibleForAutoScoring() {
        pageData.playingEdition = { origin: "local" };
        pageData.autoNotes = true;
        pageData.manualNoteLine = -1;
        pageData.notes = { 0: [{
            text: "Genius annotation for a local file.",
            source: "genius",
            sourceUrl: "https://genius.com/annotations/local-file",
            verified: true,
            votes: 12
        }] };

        tryCompare(notesColumn, "autoMode", true);
        tryCompare(notesColumn,"noteCount",1);
        verify(!notesColumn.localNotes);

        pageData.notes = { 0: [{ text: "Supplied local annotation." }] };
        tryCompare(notesColumn, "autoMode", false);
        verify(notesColumn.localNotes);
        compare(notesColumn.shownNotes[0].text, "Supplied local annotation.");
    }

    function test_keyboardHomeEndAndPageNavigation() {
        var scroll = scrollView();
        scroll.forceActiveFocus();
        tryVerify(function() { return scroll.activeFocus; });
        keyClick(Qt.Key_PageDown);
        tryVerify(function() { return scroll.contentY > 0; });
        keyClick(Qt.Key_PageUp);
        tryVerify(function() { return scroll.contentY < scroll.contentHeight - scroll.height; });
        keyClick(Qt.Key_End);
        tryVerify(function() { return scroll.atYEnd; });
        keyClick(Qt.Key_Home);
        compare(scroll.contentY, 0);
    }
    function test_wheelScrollsVisibleManualNotes() {
        var scroll=scrollView();
        verify(notesColumn.visible && scroll.visible);
        mouseWheel(scroll,100,60,0,-120);
        tryVerify(function(){return scroll.contentY>0 && pageData.notesScrollY>0;});
    }

    function test_hideShowAndRecreationRestoreSavedPosition() {
        var scroll = scrollView();
        scroll.forceActiveFocus();
        tryVerify(function() { return scroll.activeFocus; });
        keyClick(Qt.Key_End);
        tryVerify(function() { return pageData.notesScrollY > 0; });
        var saved = pageData.notesScrollY;

        notesColumn.visible = false;
        notesColumn.visible = true;
        tryCompare(scroll, "contentY", saved);

        destroyNotesColumn();
        notesColumn = createNotesColumn(pageData);
        tryVerify(function() {
            var recreatedScroll = scrollView();
            return recreatedScroll && Math.abs(recreatedScroll.contentY - saved) < 1;
        });
    }

    function test_newActiveNoteUsesAdapterResetPosition() {
        var scroll = scrollView();
        scroll.forceActiveFocus();
        tryVerify(function() { return scroll.activeFocus; });
        keyClick(Qt.Key_End);
        tryVerify(function() { return pageData.notesScrollY > 0; });

        pageData.activeNoteLine = 1;
        pageData.manualNoteLine = 1;
        pageData.notesScrollY = 0;
        tryCompare(notesColumn, "shownLineIndex", 1);
        tryCompare(scroll, "contentY", 0);
        compare(pageData.notesScrollY, 0);
    }

    function test_legacyPageDataFallsBackToLocalScrollPosition() {
        destroyNotesColumn();
        pageData.destroy();

        var notes = {};
        notes[0] = [{ text: longText("Legacy"), verified: true, votes: 20 }];
        pageData = legacyPageDataFactory.createObject(this, { notes: notes });
        notesColumn = createNotesColumn(pageData);
        tryVerify(function() { return notesColumn.initialRestoreComplete; });
        var scroll = scrollView();
        verify(!notesColumn.hasSharedScrollY);
        scroll.forceActiveFocus();
        tryVerify(function() { return scroll.activeFocus; });
        keyClick(Qt.Key_End);
        tryVerify(function() { return notesColumn.fallbackScrollY > 0; });
        var saved = notesColumn.fallbackScrollY;
        notesColumn.visible = false;
        notesColumn.visible = true;
        tryCompare(scroll, "contentY", saved);
    }

    function test_autoPickerStillSelectsOneAndCentersWhenItFits() {
        var shortNotes = {};
        shortNotes[0] = [
            { text: "A verified concise annotation.", verified: true, votes: 10 },
            { text: "A second concise annotation.", verified: true, votes: 15 }
        ];
        pageData.notes = shortNotes;
        pageData.autoNotes = true;
        pageData.manualNoteLine = -1;
        tryCompare(notesColumn, "noteCount", 1);
        var content = findChild(notesColumn, "vinylNotesContent");
        var scroll = scrollView();
        tryVerify(function() { return scroll.contentHeight === scroll.height; });
        tryVerify(function() { return content.height < scroll.height && content.y > 0; });
        verify(notesColumn.autoMode);
    }

    function startShortPassageAuto() {
        pageData.availableMs=1000;
        pageData.position=10;
        pageData.autoNotes=true;
        pageData.manualNoteLine=-1;
        tryCompare(notesColumn,"noteCount",1);
    }
    function test_longSourceShowsExcerptDuringShortLyricAndOpensFullOriginal() {
        var original=pageData.notes[0][0].text;
        startShortPassageAuto();
        verify(notesColumn.shownNotes[0].isExcerpt);
        verify(notesColumn.shownNotes[0].text.length<original.length);
        verify(original.indexOf(notesColumn.shownNotes[0].text.replace(/\u2026$/,""))===0);
        var action=findChild(notesColumn,"vinylAutoAnnotationOpen_0");
        verify(action && action.enabled);
        mouseClick(action);
        compare(pageData.manualNoteLine,0);
        verify(!notesColumn.autoMode);
        compare(notesColumn.shownNotes[0].text,original);
    }
    function test_readingHoldUsesPlaybackClockAndThenCurrentPassage() {
        startShortPassageAuto();
        var until=notesColumn.automaticUntilMs,first=notesColumn.shownNotes[0].text;
        pageData.activeNoteLine=1;
        pageData.position=10.5;
        compare(notesColumn.shownLineIndex,0);
        compare(notesColumn.shownNotes[0].text,first);
        wait(80);
        compare(notesColumn.automaticUntilMs,until,"Pausing playback must freeze the reading interval");
        pageData.position=until/1000+0.1;
        tryCompare(notesColumn,"shownLineIndex",1);
        verify(notesColumn.shownNotes[0].text!==first);
    }
    function test_holdSurvivesUnannotatedLineButSeekAndContextDoNotKeepOldNote() {
        startShortPassageAuto();
        pageData.activeNoteLine=-1;pageData.position=10.5;
        verify(notesColumn.hasAutomaticNote);
        pageData.playbackDiscontinuity(10.5);
        tryCompare(notesColumn,"noteCount",0);
        pageData.activeNoteLine=1;
        tryCompare(notesColumn,"noteCount",1);
        pageData.position=10.7;pageData.activeNoteLine=-1;
        pageData.readingContextKey="song:two";
        tryCompare(notesColumn,"noteCount",0);
    }
    function test_seekRefreshesImmediatelyInsteadOfWaitingForOldExcerpt() {
        startShortPassageAuto();
        pageData.activeNoteLine=1;pageData.position=10.5;
        compare(notesColumn.shownLineIndex,0);
        pageData.playbackDiscontinuity(10.5);
        tryCompare(notesColumn,"shownLineIndex",1);
    }
}
