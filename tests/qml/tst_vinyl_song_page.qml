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
    function containsText(object, target) {
        if (!object)
            return false;
        if (object.text === target)
            return true;
        var children = object.children || [];
        for (var i = 0; i < children.length; i++) {
            if (containsText(children[i], target))
                return true;
        }
        return false;
    }
    function activateSongOption(name) {
        waitForRendering(host.contentItem);
        mouseClick(item(name));
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
    function test_songActionsResumePinnedNotesThroughTheSharedState() {
        host.pageData.autoNotes=true;
        host.pageData.showNotes(4);
        verify(host.pageData.manualNoteLine>=0);
        mouseClick(item("vinylBarNotes"));
        compare(host.pageData.manualNoteLine,-1);
        compare(host.pageData.autoNotes,true);
        mouseClick(item("vinylBarNotes"));
        compare(host.pageData.autoNotes,false);
        host.pageData.pauseLyrics(123);
        mouseClick(item("vinylBarLyrics"));
        mouseClick(item("vinylBarLyrics"));
        compare(host.pageData.lyricsScrollY,123);
        compare(host.pageData.lyricsFollowing,false);
    }
    function test_creditsStayBoundedAndScroll() {
        activateSongOption("vinylBarCredits");
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
        compare(item('vinylPlayerBar').toolCount, 4);
        verify(item("vinylSongBrowseActions") === null, "Song actions belong only to the HUD");
        activateSongOption('vinylBarCredits');
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
        compare(host.pageData.position, host.pageData.lyrics[3].time);
        compare(host.pageData.manualNoteLine,3);
        host.pageData.timedLyrics = false;
        compare(host.pageData.currentLineIndex, -1);
        old = host.pageData.position;
        mouseClick(item('vinylLyricLine_2'));
        wait(Application.styleHints.mouseDoubleClickInterval+50);
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
        compare(host.pageData.position, host.pageData.lyrics[1].time);
        compare(item('vinylNotesColumn').shownLineIndex, 1);
        wait(Application.styleHints.mouseDoubleClickInterval+50);
        compare(host.pageData.position, host.pageData.lyrics[1].time);
        mouseClick(item('vinylLyricLine_4'), 10, 10, Qt.RightButton);
        compare(item('vinylNotesColumn').shownLineIndex, 4);
        compare(host.pageData.position, host.pageData.lyrics[1].time);
    }
    function test_autoNotes() {
        host.pageData.toggleNotes();
        host.pageData.seekToLine(1);
        tryCompare(item('vinylNotesColumn'),"noteCount",1);
        tryCompare(item('vinylNotesColumn'),"shownLineIndex",1);
        host.pageData.seekToLine(2);
        tryCompare(item('vinylNotesColumn'),"shownLineIndex",1);
        host.pageData.seekToLine(4);
        tryCompare(item('vinylNotesColumn'),"shownLineIndex",4);
        host.pageData.toggleLyrics();
        verify(!item('vinylNotesColumn').visible);
    }
    function test_responsiveColumnsKeepCenteredArtwork() {
        var page = item("vinylSongPage");
        var poster = item("vinylSongPoster");
        var lyrics = item("vinylLyricsColumn");
        var notes = item("vinylNotesColumn");
        var lyricScroll = item("vinylLyricsScroll");
        var frame = item("vinylSongVideoFrame");
        var metadata = item("vinylSongMetadata");
        var back = item("vinylSongBack");
        var sizes = [
            { width: 1280, height: 720 },
            { width: 1280, height: 672 },
            { width: 1600, height: 900 },
            { width: 960, height: 720 },
            { width: 800, height: 720 }
        ];
        verify(page && poster && lyrics && notes && lyricScroll && frame && metadata && back);
        verify(page.videoSurface === frame, "videoSurface must expose the actual frame Item");
        compare(frame.objectName, "vinylSongVideoFrame");
        verify(item("vinylCompactLyrics") === null, "Compact lyrics overlay must be removed");
        host.pageData.autoNotes = true;
        host.pageData.manualNoteLine = -1;
        host.pageData.seekToLine(1);
        host.pageData.lyricsShown = true;

        for (var i = 0; i < sizes.length; ++i) {
            var size = sizes[i];
            host.width = size.width;
            host.height = size.height;
            host.requestUpdate();
            tryCompare(page, "width", size.width);
            waitForRendering(host.contentItem);
            verify(lyrics.visible, "Lyrics must stay on the left at " + size.width + "px");
            verify(notes.visible, "Annotations must stay on the right at " + size.width + "px");
            var readingTop = lyrics.mapToItem(page, 0, 0).y;
            var viewportBottom = lyricScroll.mapToItem(page, 0, lyricScroll.height).y;
            verify(lyricScroll.height >= poster.height + 24,
                   "The visible lyric viewport must extend well below poster height at " + size.width + "px");
            verify(lyricScroll.height >= (page.height - readingTop) * 0.75,
                   "The visible lyric viewport must use most of the available Song body at " + size.width + "px");
            verify(viewportBottom <= page.height - 7,
                   "The viewport must end before the Song body and status row at " + size.width + "px");
            console.log("FULL_HEIGHT_GEOMETRY " + size.width + "x" + size.height
                + " page=" + page.height.toFixed(1)
                + " poster=" + poster.height.toFixed(1)
                + " lyricColumn=" + lyrics.height.toFixed(1)
                + " lyricViewport=" + lyricScroll.height.toFixed(1)
                + " top=" + readingTop.toFixed(1)
                + " viewportBottom=" + viewportBottom.toFixed(1));
            verify(lyrics.width >= 160, "Lyrics column should remain usable at " + size.width + "px");
            verify(notes.width >= 160, "Annotations column should remain usable at " + size.width + "px");
            verify(lyrics.x + lyrics.width <= poster.x + 0.5,
                   "Lyrics must not overlap artwork at " + size.width + "px");
            verify(notes.x >= poster.x + poster.width - 0.5,
                   "Annotations must not overlap artwork at " + size.width + "px");
            verify(Math.abs(poster.x + poster.width / 2 - page.width / 2) < 0.5,
                   "Artwork must remain centered at " + size.width + "px");

            var posterX = poster.x;
            var posterY = poster.y;
            var posterWidth = poster.width;
            var posterHeight = poster.height;
            host.pageData.lyricsShown = false;
            tryVerify(function() { return !lyrics.visible && !notes.visible; });
            verify(Math.abs(poster.x - posterX) < 0.01, "Artwork x must not change when columns close");
            verify(Math.abs(poster.y - posterY) < 0.01, "Artwork y must not change when columns close");
            verify(Math.abs(poster.width - posterWidth) < 0.01, "Artwork width must not change when columns close");
            verify(Math.abs(poster.height - posterHeight) < 0.01, "Artwork height must not change when columns close");

            host.pageData.lyricsShown = true;
            host.pageData.videoShown = true;
            tryVerify(function() { return !lyrics.visible && !notes.visible; });
            verify(frame.visible, "Video frame must remain visible at " + size.width + "px");
            verify(frame.width <= page.width + 0.5,
                   "Video frame must fit within the bounded width at " + size.width + "px");
            verify(Math.abs(frame.width / frame.height - 16 / 9) < 0.001,
                   "Video frame must keep its aspect ratio at " + size.width + "px");
            var frameBounds = frame.mapToItem(page, 0, 0);
            verify(frameBounds.y >= 0,
                   "Video frame must stay inside the stage at " + size.width + "px");
            verify(frameBounds.y + frame.height <= page.height + 0.5,
                   "Video frame must stay inside the page at " + size.width + "px");
            verify(!metadata.visible, "Duplicate video metadata must be hidden");
            var expectedHeight = Math.min(page.height, page.width * 9 / 16);
            verify(Math.abs(frame.height - expectedHeight) < 0.5,
                   "Video must use the available Song body at " + size.width + "px");
            verify(frame.height > page.centerWidth,
                   "Video must exceed the artwork size cap at " + size.width + "px");
            verify(!containsText(page, "Video artwork"),
                   "The placeholder label must not appear at " + size.width + "px");
            host.pageData.videoShown = false;
            tryVerify(function() { return lyrics.visible && notes.visible; });
        }
    }
    function test_regularVideoUsesWholeStageAndOverlayBack_data() {
        return [{tag:"desktop",width:1280,height:720},{tag:"narrow",width:800,height:720}];
    }
    function test_regularVideoUsesWholeStageAndOverlayBack(data) {
        host.width=data.width;host.height=data.height;
        host.pageData.videoShown=true;
        var page=item("vinylSongPage"),bar=item("vinylPlayerBar");
        // Supply the body under a header, with no footer reservation, like the live video route.
        page.anchors.bottom=undefined;
        page.anchors.topMargin=80;
        page.height=host.height-80;
        waitForRendering(host.contentItem);
        var frame=page.videoSurface,back=item("vinylSongBack"),stage=item("vinylSongVideoStage");
        compare(stage.width,page.width);compare(stage.height,page.height);
        verify(stage.visible);compare(stage.color,Qt.rgba(0,0,0,1));
        compare(frame.height,Math.min(page.height,page.width*9/16));
        verify(Math.abs(frame.width/frame.height-16/9)<0.001);
        var origin=frame.mapToItem(page,0,0);
        verify(Math.abs(origin.x-(page.width-frame.width)/2)<0.5);
        verify(Math.abs(origin.y-(page.height-frame.height)/2)<0.5);
        verify(!item("vinylSongMetadata").visible);
        verify(back.visible && back.width>=44 && back.height>=44);
        verify(back.x>=origin.x && back.x+back.width<=origin.x+frame.width);
        verify(back.y>=origin.y && back.y+back.height<=origin.y+frame.height);
        verify(back.z>frame.parent.z,"Back must stack over the video and its pointer layer");
        verify(item("vinylSongBackScrim").visible);
        var requests=0,fullscreenRequests=0,position=host.pageData.position,paused=host.pageData.paused;
        page.backRequested.connect(function(){requests++;});
        page.videoFullscreenRequested.connect(function(){fullscreenRequests++;});
        mouseClick(back,back.width/2,back.height/2);
        compare(requests,1);compare(fullscreenRequests,0);
        compare(host.pageData.position,position);compare(host.pageData.paused,paused);
    }

    function test_videoHoldsPosition() {
        host.pageData.toggleLyrics();
        host.pageData.toggleNotes();
        host.pageData.seekToLine(1);
        var page = item('vinylSongPage');
        var lyrics = item('vinylLyricsColumn');
        var notes = item('vinylNotesColumn');
        var previousLyricsVisible = lyrics.visible;
        var previousNotesVisible = notes.visible;
        verify(previousLyricsVisible && previousNotesVisible,
               "Reader columns should be visible before opening video");
        var old = host.pageData.position;
        var albumRequestCount = 0;
        page.albumRequested.connect(function() { albumRequestCount += 1; });
        activateSongOption('vinylBarVideo');
        compare(host.pageData.position, old);
        var frame = item('vinylSongVideoFrame');
        verify(page.videoSurface === frame);
        compare(frame.objectName, "vinylSongVideoFrame");
        verify(frame.visible);
        verify(!containsText(page, "Video artwork"));
        verify(Math.abs(frame.width / frame.height - 16 / 9) < 0.001);
        verify(!lyrics.visible);
        verify(!notes.visible);
        mouseClick(frame, frame.width / 2, frame.height / 2);
        compare(albumRequestCount, 0, "The album overlay must not receive clicks during video");
        activateSongOption('vinylBarVideo');
        compare(host.pageData.position, old);
        compare(lyrics.visible, previousLyricsVisible);
        compare(notes.visible, previousNotesVisible);
    }

    function test_fullscreenReusesSurfaceAndRestoresGeometry_data() {
        return [{tag:"desktop",width:1280,height:720},{tag:"narrow",width:800,height:720}];
    }
    function test_fullscreenReusesSurfaceAndRestoresGeometry(data) {
        host.width=data.width;host.height=data.height;
        host.pageData.videoShown=true;
        host.pageData.position=83;
        var page=item("vinylSongPage"),frame=page.videoSurface,bar=item("vinylPlayerBar");
        waitForRendering(host.contentItem);
        var ordinaryParent=frame.parent, ordinaryWidth=frame.width, ordinaryHeight=frame.height;
        var position=host.pageData.position, paused=host.pageData.paused;
        var edition=host.pageData.playingEditionId, track=host.pageData.playingIndex;
        var attached=Qt.createQmlObject('import QtQuick; Item { property string identity: "existing-player-surface" }',frame);
        mouseClick(item("vinylBarFullscreen"));
        tryCompare(host,"videoFullscreen",true);
        waitForRendering(host.contentItem);
        verify(page.videoSurface===frame);
        compare(frame.parent,page);
        compare(attached.parent,frame);
        compare(attached.identity,"existing-player-surface");
        compare(frame.width,data.width);compare(frame.height,data.height);
        compare(frame.mapToItem(host.contentItem,0,0),Qt.point(0,0));
        verify(!item("vinylSongBack").visible);
        verify(!item("vinylSongMetadata").visible);
        verify(frame.visible);
        compare(host.pageData.position,position);compare(host.pageData.paused,paused);
        compare(host.pageData.playingEditionId,edition);compare(host.pageData.playingIndex,track);
        mouseClick(item("vinylBarFullscreen"));
        tryCompare(host,"videoFullscreen",false);
        waitForRendering(host.contentItem);
        verify(page.videoSurface===frame);
        compare(frame.parent,ordinaryParent);
        compare(frame.width,ordinaryWidth);compare(frame.height,ordinaryHeight);
        compare(attached.parent,frame);
        compare(host.pageData.position,position);compare(host.pageData.paused,paused);
        verify(item("vinylSongBack").visible);
        mouseClick(item("vinylBarFullscreen"));
        verify(host.videoFullscreen);
        mouseClick(item("vinylBarVideo"));
        verify(!host.pageData.videoShown);verify(!host.videoFullscreen);
        compare(frame.parent,ordinaryParent);
        attached.destroy();
    }
    function test_videoPointerOnlyWakesControlsAndDoubleClickRequestsFullscreen() {
        var page=item("vinylSongPage");
        host.pageData.videoShown=true;
        waitForRendering(host.contentItem);
        var pointer=item("vinylSongVideoPointer");
        var interactions=0, requests=0;
        page.videoInteraction.connect(function(){interactions++;});
        page.videoFullscreenRequested.connect(function(){requests++;});
        var paused=host.pageData.paused,position=host.pageData.position;
        mouseClick(pointer,pointer.width/2,pointer.height/2);
        verify(interactions>0);compare(requests,0);verify(!host.videoFullscreen);
        compare(host.pageData.paused,paused);compare(host.pageData.position,position);
        mouseDoubleClickSequence(pointer,pointer.width/2,pointer.height/2);
        compare(requests,1);verify(host.videoFullscreen);
        compare(host.pageData.paused,paused);compare(host.pageData.position,position);
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
        host.pageData.lyricsShown=true;
        verify(!item('vinylLyricsColumn').visible);
    }
    function test_manualNoteStaysPinnedUntilAutoResumed() {
        host.pageData.toggleNotes();
        host.pageData.seekToLine(1);
        host.pageData.showNotes(4);
        compare(item('vinylNotesColumn').shownLineIndex, 4);
        host.pageData.seekToLine(2);
        compare(item('vinylNotesColumn').shownLineIndex, 4);
        host.pageData.seekToLine(4);
        compare(host.pageData.manualNoteLine, 4);
        verify(item('vinylNotesColumn').noteCount >= 1);
        host.pageData.toggleLyrics();
        host.pageData.toggleLyrics();
        compare(host.pageData.manualNoteLine,4);
        host.page = 'album';
        host.page = 'song';
        verify(host.pageData.lyricsShown);
        verify(host.pageData.autoNotes);
        compare(host.pageData.manualNoteLine,4);
        mouseClick(item('vinylBarNotes'));
        compare(host.pageData.manualNoteLine,-1);
        verify(host.pageData.autoNotes);
        tryCompare(item('vinylNotesColumn'),"noteCount",1);
        mouseClick(item('vinylBarNotes'));
        verify(!host.pageData.autoNotes);
    }
    function test_manualScrollRestoresAndReturnDoesNotSeekOrUnpin() {
        host.pageData.toggleLyrics();host.pageData.toggleNotes();host.pageData.showNotes(4);
        var lyrics=item('vinylLyricsColumn'),scroll=item('vinylLyricsScroll');
        scroll.forceActiveFocus();keyClick(Qt.Key_PageDown);
        verify(!host.pageData.lyricsFollowing);
        verify(lyrics.contentY>0);
        var y=lyrics.contentY,position=host.pageData.position;
        host.pageData.seekToLine(4);
        compare(lyrics.contentY,y);
        host.pageData.toggleLyrics();host.pageData.toggleLyrics();
        tryCompare(lyrics,'contentY',y);
        compare(host.pageData.manualNoteLine,4);
        host.page='album';host.page='song';
        lyrics=item('vinylLyricsColumn');
        tryCompare(lyrics,'contentY',y);
        position=host.pageData.position;
        mouseClick(item('vinylLyricsResume'));
        verify(host.pageData.lyricsFollowing);
        compare(host.pageData.position,position);
        compare(host.pageData.manualNoteLine,4);
    }
    function test_immediateSeekIsNotRepeatedAfterVisibilityOrTrackChanges() {
        host.pageData.toggleLyrics();
        var before=host.pageData.position;
        mouseClick(item('vinylLyricLine_2'));
        host.pageData.toggleLyrics();
        wait(Application.styleHints.mouseDoubleClickInterval+50);
        compare(host.pageData.position,host.pageData.lyrics[2].time);
        host.pageData.toggleLyrics();
        mouseClick(item('vinylLyricLine_2'));
        host.pageData.next(1);
        wait(Application.styleHints.mouseDoubleClickInterval+50);
        compare(host.pageData.position,0);
        verify(host.pageData.lyricsFollowing);
        compare(host.pageData.manualNoteLine,-1);
    }
    function test_oneClickFollowsAutoAndReplacesManualNotes() {
        host.pageData.toggleLyrics();host.pageData.toggleNotes();
        host.pageData.seekToLine(1);
        mouseClick(item("vinylLyricLine_4"),10,10);
        compare(host.pageData.position,host.pageData.lyrics[4].time);
        compare(host.pageData.manualNoteLine,-1);verify(host.pageData.autoNotes);
        tryCompare(item("vinylNotesColumn"),"shownLineIndex",4);
        host.pageData.seekToLine(1);
        tryCompare(item("vinylNotesColumn"),"shownLineIndex",1);
        host.pageData.showNotes(4);
        mouseClick(item("vinylLyricLine_1"),10,10);
        compare(host.pageData.manualNoteLine,1);
        compare(item("vinylNotesColumn").shownLineIndex,1);
        mouseClick(item("vinylLyricLine_2"),10,10);
        compare(host.pageData.manualNoteLine,2);
        compare(host.pageData.activeNoteLine,-1);
        verify(!item("vinylNotesColumn").visible);
    }
}
