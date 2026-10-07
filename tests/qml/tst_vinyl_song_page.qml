import QtQuick
import QtTest

TestCase {
    name: "VinylSongPage"
    when: windowShown
    property var host
    function item(name) {
        function walk(o) {
            if (o.objectName === name)
                return o;
            var children = o.children || [];
            for (var i = 0; i < children.length; i++) {
                var f = walk(children[i]);
                if (f)
                    return f;
            }
            return null;
        }
        return findChild(host, name) || walk(host.contentItem);
    }
    function init() {
        var c = Qt.createComponent('../../qml/vinyl/VinylPagesPrototype.qml');
        verify(c.status === Component.Ready, c.errorString());
        host = c.createObject(null);
        host.pageData.frozen = true;
        host.pageData.setFixture('without-me');
        host.page = 'song';
        tryCompare(host, 'ready', true);
    }
    function cleanup() {
        if (host) {
            host.close();
            host.destroy();
        }
    }
    function test_visibleBackPreservesPlayback() {
        var back = item("vinylSongBack");
        verify(back !== null, "Song page needs a visible Back control");
        verify(back.visible && back.width >= 40 && back.height >= 40);
        var edition = host.pageData.playingEditionId;
        var position = host.pageData.position;
        mouseClick(back);
        compare(host.page, "album");
        compare(host.pageData.playingEditionId, edition);
        compare(host.pageData.position, position);
        host.page = "song";
        back.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.page, "album");
    }
    function test_creditsStayBoundedAndScroll() {
        mouseClick(item("vinylBarCredits"));
        var popup = item("vinylCreditsPopup");
        tryCompare(popup, "opened", true);
        var oldHeight = popup.height;
        var edition = JSON.parse(JSON.stringify(host.pageData.playingEdition));
        edition.credits = [];
        for (var n = 0; n < 30; ++n)
            edition.credits.push({
                role: "Fixture role " + n,
                names: ["Original stand-in " + n]
            });
        host.pageData.playingEdition = edition;
        compare(popup.height, oldHeight, "Credit count must not grow the popup");
        var scroll = item("vinylCreditsScroll");
        verify(scroll !== null);
        tryVerify(function () {
            return scroll.contentHeight > scroll.height;
        });
        compare(popup.height, oldHeight, "Height stays fixed after credit layout");
        scroll.forceActiveFocus();
        keyClick(Qt.Key_PageDown);
        tryVerify(function () {
            return scroll.contentY > 0;
        });
        keyClick(Qt.Key_End);
        tryVerify(function () {
            return scroll.atYEnd;
        });
        keyClick(Qt.Key_Home);
        compare(scroll.contentY, 0);
        keyClick(Qt.Key_Escape);
        tryCompare(popup, "opened", false);
    }
    function test_restAndTools() {
        verify(!item('vinylLyricsColumn').visible);
        verify(!item('vinylNotesColumn').visible);
        var poster = item('vinylSongPoster');
        compare(poster.width / poster.height, 1);
        compare(item('vinylPlayerBar').toolCount, 6);
        mouseClick(item('vinylBarCredits'));
        tryCompare(item('vinylCreditsPopup'), 'opened', true);
        keyClick(Qt.Key_Escape);
        tryCompare(item('vinylCreditsPopup'), 'opened', false);
        var title = item('vinylSongPage').songTitle;
        mouseClick(item('vinylBarNext'));
        verify(item('vinylSongPage').songTitle !== title);
        host.pageData.setFixture('mounamela');
        verify(poster.width / poster.height < 1);
        verify(!item('vinylBarVideo').enabled);
        verify(!item('vinylBarNotes').enabled);
        host.page = 'album';
        compare(item('vinylPlayerBar').toolCount, 1);
    }
    function test_lyricsGeometryAndSeek() {
        var poster = item('vinylSongPoster');
        var x = poster.x;
        var y = poster.y;
        mouseClick(item('vinylBarLyrics'));
        verify(item('vinylLyricsColumn').visible);
        compare(poster.x, x);
        compare(poster.y, y);
        var line = item('vinylLyricLine_3');
        tryVerify(function () {
            return line.opacity > 0.99 && line.mapToItem(item("vinylLyricsColumn"), 0, 0).y >= 0;
        });
        var old = host.pageData.position;
        mouseClick(line);
        compare(host.pageData.position, old);
        tryCompare(host.pageData, 'position', host.pageData.lyrics[3].time);
        host.pageData.timedLyrics = false;
        compare(host.pageData.currentLineIndex, -1);
        old = host.pageData.position;
        mouseClick(item('vinylLyricLine_2'));
        wait(300); // The product's 250ms delayed action must remain inert for untimed lyrics.
        compare(host.pageData.position, old);
    }
    function test_doubleAndRightClick() {
        host.pageData.toggleLyrics();
        var line = item('vinylLyricLine_1');
        tryVerify(function () {
            return line.opacity > 0.99 && line.mapToItem(item("vinylLyricsColumn"), 0, 0).y >= 0;
        });
        var old = host.pageData.position;
        mouseDoubleClickSequence(line, line.width / 2, line.height / 2, Qt.LeftButton);
        compare(host.pageData.position, old);
        compare(item('vinylNotesColumn').shownLineIndex, 1);
        wait(300); // Assert no delayed seek escapes the cancelled double-click.
        compare(host.pageData.position, old);
        mouseClick(item('vinylLyricLine_4'), 10, 10, Qt.RightButton);
        compare(item('vinylNotesColumn').shownLineIndex, 4);
        compare(host.pageData.position, old);
    }
    function test_autoNotes() {
        host.pageData.toggleNotes();
        host.pageData.seekToLine(1);
        compare(item('vinylNotesColumn').noteCount, 1);
        compare(item('vinylNotesColumn').shownLineIndex, 1);
        host.pageData.seekToLine(2);
        compare(item('vinylNotesColumn').shownLineIndex, 1);
        host.pageData.seekToLine(4);
        compare(item('vinylNotesColumn').shownLineIndex, 4);
        host.pageData.toggleLyrics();
        verify(!item('vinylNotesColumn').visible);
    }
    function test_videoHoldsPosition() {
        host.pageData.toggleLyrics();
        host.pageData.toggleNotes();
        host.pageData.seekToLine(1);
        var old = host.pageData.position;
        mouseClick(item('vinylBarVideo'));
        compare(host.pageData.position, old);
        var frame = item('vinylSongVideoFrame');
        verify(Math.abs(frame.width / frame.height - 16 / 9) < 0.001);
        verify(!item('vinylLyricsColumn').visible);
        verify(!item('vinylNotesColumn').visible);
        mouseClick(item('vinylBarVideo'));
        compare(host.pageData.position, old);
        verify(item('vinylLyricsColumn').visible);
        verify(item('vinylNotesColumn').visible);
    }

    function test_captureStates() {
        if (typeof VinylCaptureDirectory === "undefined" || !VinylCaptureDirectory)
            skip("Set VINYL_CAPTURE_DIR for visual captures");
        host.developerStrip = false;
        for (var size = 0; size < 2; size++) {
            host.width = size ? 1600 : 1280;
            host.height = size ? 900 : 720;
            host.pageData.setFixture('without-me');
            host.pageData.position = 36;
            var states = ['rest', 'lyrics', 'notes', 'auto', 'video', 'sagara'];
            for (var i = 0; i < states.length; i++) {
                host.pageData.lyricsShown = i > 0;
                host.pageData.autoNotes = i === 3;
                host.pageData.manualNoteLine = i === 2 ? 1 : -1;
                host.pageData.videoShown = i === 4;
                if (i === 5)
                    host.pageData.setFixture('mounamela');
                host.requestUpdate();
                waitForRendering(host.contentItem);
                var capture = grabImage(host.contentItem);
                compare(capture.width, host.width);
                capture.save(VinylCaptureDirectory + '/song-' + host.width + '-' + states[i] + '.png');
            }
        }
    }

    function test_captureCreditsOverflow() {
        if (typeof VinylCaptureDirectory === "undefined" || !VinylCaptureDirectory)
            skip("Set VINYL_CAPTURE_DIR for visual captures");
        host.developerStrip = false;
        var edition = JSON.parse(JSON.stringify(host.pageData.playingEdition));
        edition.credits = [];
        for (var n = 0; n < 30; ++n)
            edition.credits.push({
                role: "Fixture role " + n,
                names: ["Original stand-in " + n]
            });
        host.pageData.playingEdition = edition;
        for (var size = 0; size < 2; ++size) {
            host.width = size ? 1600 : 1280;
            host.height = size ? 900 : 720;
            item("vinylCreditsPopup").open();
            tryCompare(item("vinylCreditsPopup"), "opened", true);
            host.requestUpdate();
            waitForRendering(host.contentItem);
            var capture = grabImage(host.contentItem);
            compare(capture.width, host.width);
            capture.save(VinylCaptureDirectory + "/credits-overflow-" + host.width + ".png");
            item("vinylCreditsPopup").close();
        }
    }

    function test_bareSongTools() {
        var e = JSON.parse(JSON.stringify(host.pageData.playingEdition));
        e.hasLyrics = false;
        e.hasNotes = false;
        e.hasVideo = false;
        host.pageData.playingEdition = e;
        verify(!item('vinylBarLyrics').enabled);
        verify(!item('vinylBarVideo').enabled);
        verify(!item('vinylBarNotes').enabled);
    }
    function test_manualNoteSupersededByNextLine() {
        host.pageData.toggleNotes();
        host.pageData.seekToLine(1);
        host.pageData.showNotes(4);
        compare(item('vinylNotesColumn').shownLineIndex, 4);
        host.pageData.seekToLine(2);
        compare(item('vinylNotesColumn').shownLineIndex, 4);
        host.pageData.seekToLine(4);
        compare(host.pageData.manualNoteLine, -1);
        compare(item('vinylNotesColumn').noteCount, 1);
        host.page = 'album';
        host.page = 'song';
        verify(host.pageData.lyricsShown);
        verify(host.pageData.autoNotes);
    }
}
