import QtQuick
import QtTest
import "../../qml/vinyl/VinylArtistCatalogue.js" as Catalogue

TestCase {
    name: "VinylArtistPage"
    when: windowShown
    property var host
    function visibleItem(parent, name) {
        if (!parent || !parent.visible) return null;
        if (parent.objectName === name) return parent;
        var children = parent.children || [];
        for (var i = 0; i < children.length; ++i) {
            var found = visibleItem(children[i], name);
            if (found) return found;
        }
        return null;
    }
    function item(name) {
        // Compact and legacy sections may contain identically named headers.
        // Target the visible control; Popup/Timer resources need the Window tree.
        return visibleItem(host.contentItem, name)
            || findChild(host.contentItem, name) || findChild(host, name);
    }
    function init() {
        var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
        verify(component.status === Component.Ready, component.errorString());
        host = component.createObject(null);
        verify(host !== null);
        compare(host.artistData.name, "Eminem");
        host.artistData.open("Ilaiyaraaja");
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
    }
    function cleanup() {
        if (host) { host.close(); host.destroy(); }
    }
    function test_creditProjectionDoesNotMergeUnrelatedArtists() {
        var source = {albums: [
            {id:"a",title:"Album",year:2000,artists:["A.R. Rahman"],tracks:[
                {title:"Song",artists:["S. P. Balasubrahmanyam"],seconds:120}]},
            {id:"b",title:"Other",year:2001,artists:["Shankar Mahadevan"],tracks:[]}
        ]};
        compare(Catalogue.artist(source, "A. R. Rahman").releases[0].credit, "album");
        compare(Catalogue.artist(source, "S. P. Balasubrahmanyam").releases[0].credit, "track");
        compare(Catalogue.artist(source, "K. V. Mahadevan").releases.length, 0);
        compare(Catalogue.artist(source, "Kavita Subrahmanyam").songs.length, 0);
    }
    function test_releaseHistoryUsesAvailableCatalogueYears() {
        var record = {releases: [
            {album:{title:"Recent",year:2024},credit:"album"},
            {album:{title:"Early",year:2000},credit:"album"},
            {album:{title:"Guest",year:2010},credit:"track"},
            {album:{title:"Own",year:2010},credit:"album"},
            {album:{title:"Undated"},credit:"album"},
            {album:{title:"Middle",year:2015},credit:"album"},
            {album:{title:"Later",year:2020},credit:"album"}
        ]};
        var history = Catalogue.releaseHistory(record, [], 4);
        compare(history.length, 4);
        compare(history[0].album.title, "Early");
        compare(history[1].album.title, "Own");
        compare(history[3].album.title, "Recent");
        compare(Catalogue.releaseHistory({releases:[record.releases[0]]}, [], 4).length, 1);
        compare(Catalogue.releaseHistory({releases:[]}, [], 4).length, 0);
        compare(Catalogue.releaseHistory({releases:[]}, [{title:"Only single",year:2021,releaseType:"Single"}], 4)[0].album.title, "Only single");
    }
    function test_worldArtistAndCreditRoutesPreservePlayback() {
        var playing = host.pageData.playingEditionId;
        var position = host.pageData.position;
        host.page = "world";
        host.openWorldItem({kind:"artists",title:"Michael Jackson"});
        compare(host.page, "artist");
        compare(host.artistData.record.name, "Michael Jackson");
        compare(host.artistData.record.releases.length, 3);
        compare(host.pageData.playingEditionId, playing);
        compare(host.pageData.position, position);
        host.artistBack();
        compare(host.page, "world");
        host.pageData.artistRequested("Ilayaraja");
        compare(host.page, "artist");
        compare(host.artistData.record.name, "Ilaiyaraaja");
    }
    function test_albumRoundTripKeepsArtistPositionAndPlayingState() {
        host.artistData.newestFirst = false;
        host.artistScroll.contentY = 100;
        var expected = host.artistData.releases[0].album.title;
        var playing = host.pageData.playingEditionId;
        item("vinylArtistDiscographyCard_0").activate();
        compare(host.page, "album");
        compare(host.pageData.shownEdition.name, expected);
        compare(host.pageData.playingEditionId, playing);
        item("vinylAlbumBack").activate();
        compare(host.page, "artist");
        compare(host.artistData.newestFirst, false);
        compare(host.artistScroll.contentY, 100);
    }
    function test_nestedArtistBackRestoresOriginalAlbum() {
        host.page = "album";
        var album = host.pageData.shownEditionId;
        host.openArtist("Michael Jackson");
        item("vinylArtistDiscographyCard_0").activate();
        item("vinylAlbumBack").activate();
        host.openArtist("Ilaiyaraaja");
        host.artistBack();
        compare(host.artistData.record.name, "Michael Jackson");
        host.artistBack();
        compare(host.page, "album");
        compare(host.pageData.shownEditionId, album);
        item("vinylAlbumBack").activate();
        compare(host.page, "world");
    }
    function test_songSelectionAndSongPageReturn() {
        var expected = host.artistData.singlesRows[0].song;
        item("vinylArtistTab_singles").activate();
        // This artist has no provider-ranked songs; its visible song surface is
        // the native compact Singles list, not the hidden popular-song instance.
        item("vinylArtistSinglesRow_0").activate();
        compare(host.page, "artist");
        compare(host.pageData.playingEditionId, expected.album.id);
        compare(host.pageData.playingTrack.title, expected.track.title);
        host.openSong();
        compare(host.songReturnPage, "artist");
        compare(host.page, "song");
        item("vinylSongArtistLink").activate();
        compare(host.page, "artist");
        host.artistBack();
        compare(host.page, "song");
        // Song uses the shared BackAction: exercise its real pointer input,
        // then its keyboard input rather than a KeyboardAction-only helper.
        var playing = host.pageData.playingEditionId;
        var track = host.pageData.playingTrack.title;
        var position = host.pageData.position;
        mouseClick(item("vinylSongBack"));
        compare(host.page, "artist");
        compare(host.pageData.playingEditionId, playing);
        compare(host.pageData.playingTrack.title, track);
        compare(host.pageData.position, position);
        host.openSong();
        compare(host.page, "song");
        item("vinylSongBack").forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.page, "artist");
        compare(host.pageData.playingEditionId, playing);
        compare(host.pageData.playingTrack.title, track);
        compare(host.pageData.position, position);
    }
    function test_multipleNamesAreNotTreatedAsOneIdentity() {
        host.openArtist("Michael Jackson · Joni Mitchell");
        compare(host.artistChoices.length, 2);
        compare(host.artistData.record.name, "Ilaiyaraaja");
        tryVerify(function() { return item("vinylArtistChoice_1") !== null; });
        item("vinylArtistChoice_1").activate();
        compare(host.artistData.record.name, "Joni Mitchell");
    }
    function test_sortFilterAndSaveKeyboard() {
        var first = host.artistData.releases[0].album.year;
        var sort = item("vinylArtistSort");
        sort.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.artistData.newestFirst, false);
        verify(host.artistData.releases[0].album.year < first);
        var save = item("vinylArtistSave");
        save.forceActiveFocus();
        keyClick(Qt.Key_Return);
        verify(host.artistData.saved);
        host.openArtist("Michael Jackson");
        verify(!host.artistData.saved);
        host.artistBack();
        verify(host.artistData.saved);
        host.openArtist("S. P. Balasubrahmanyam");
        host.artistData.filter = "track"; // Data credit projection; legacy filter UI is retired.
        compare(host.artistData.filter, "track");
        verify(host.artistData.releases.length > 0);
        verify(host.artistData.releases.every(function(r) { return r.credit === "track"; }));
    }
    function test_thinAndUnknownArtist() {
        host.openArtist("Joni Mitchell");
        compare(host.artistData.releases.length, 1);
        compare(host.artistData.releaseHistory.length, 1);
        host.openArtist("Uncatalogued artist");
        compare(host.artistData.releases.length, 0);
        compare(host.artistData.songs.length, 0);
        compare(host.artistData.releaseHistory.length, 0);
        compare(item("vinylArtistName").text, "Uncatalogued artist");
        verify(item("vinylArtistSave").visible);
    }
    function test_releaseHistoryWithoutAuthoredBiography() {
        host.artistData.open("S. P. Balasubrahmanyam");
        compare(host.artistData.context.history, "");
        verify(host.artistData.releaseHistory.length > 0);
        verify(item("vinylArtistHistory_0") === null);
        verify(item("vinylArtistAbout") === null);
        var selected = host.artistData.discographyCards[0].album;
        item("vinylArtistDiscographyCard_0").activate();
        compare(host.page, "album");
        compare(host.pageData.shownEdition.name, selected.title);
    }
    function test_eminemSectionsPlaylistAndSingleRoundTrip() {
        host.artistData.open("Eminem");
        verify(host.artistData.releases.length >= 6);
        compare(host.artistData.singles.length, 3);
        compare(host.artistData.releaseHistory.length, 4);
        compare(Number(host.artistData.releaseHistory[0].album.year), 2002);
        verify(item("vinylArtistHistory_0") === null);
        verify(!host.artistData.createPlaylist("   "));
        verify(host.artistData.createPlaylist("My Eminem mix"));
        compare(host.artistData.playlists.length, 1);
        compare(host.artistData.playlists[0].songs.length, 5);
        verify(item("vinylArtistPlaylist_0") === null);
        var playing = host.pageData.playingEditionId;
        item("vinylArtistTab_singles").activate();
        compare(host.artistData.activeTab, "singles");
        compare(host.artistData.singlesRows.length, 43);
        var row = host.artistData.singlesRows.findIndex(function(r) { return r.album && r.album.tracks && r.album.tracks.length > 0; });
        verify(row >= 0);
        var first = host.artistData.singlesRows[row].album;
        item("vinylArtistSinglesAlbum_" + row).activate();
        compare(host.page, "album");
        compare(host.pageData.shownEdition.name, first.title);
        item("vinylAlbumBack").activate();
        compare(host.page, "artist");
        compare(host.artistData.activeTab, "singles");
        compare(host.pageData.playingEditionId, playing);
        host.artistData.open("Joni Mitchell");
        compare(host.artistData.playlists.length, 0);
        compare(host.artistData.releaseHistory.length, 1);
        host.artistData.open("Eminem");
        compare(host.artistData.playlists[0].name, "My Eminem mix");
    }
    function test_referenceBreadthIsDataGated() {
        host.artistData.open("Eminem");
        verify(host.artistData.media.videos.length >= 3);
        verify(host.artistData.media.relatedArtists.length >= 3);
        verify(host.artistData.media.portrait.cover.length > 0);
        compare(host.artistData.media.livePerformances.length, 0);
        compare(host.artistData.media.featuredPlaylists.length, 0);
        compare(host.artistData.media.artistPlaylists.length, 0);
        compare(host.artistData.playlists.length, 0);
        verify(item("vinylArtistLiveSection") === null);
        verify(item("vinylArtistFeaturedSection") === null);
        verify(item("vinylArtistArtistPlaylistsSection") === null);
        verify(item("vinylArtistLibrarySection") === null);
        var playing = host.pageData.playingEditionId;
        item("vinylArtistTab_videos").activate();
        item("vinylArtistVideoCard_0").activate();
        tryVerify(function() { return item("vinylArtistMediaSource") !== null; });
        compare(item("vinylArtistMediaSource").accessibleDescription, host.artistData.media.videos[0].sourceUrl);
        compare(host.pageData.playingEditionId, playing);
        keyClick(Qt.Key_Escape);
        tryVerify(function() { var source = item("vinylArtistMediaSource"); return !source || !source.visible; });
        var related = host.artistData.media.relatedArtists[0].title;
        host.openArtist(related); // Related navigation is routed from artist links, not the retired shelf.
        compare(host.artistData.record.name, related);
    }
    function test_optionalSectionsNeedRecordsAndProvenance() {
        host.artistData.open("Test artist");
        var record = {id:"fixture",title:"Test record",sourceUrl:"https://example.test/record",cover:""};
        host.artistData.mediaCatalogue = {"Test artist": {
            name:"Test artist",provider:"Test provider",sourceUrl:"https://example.test/artist",
            livePerformances:[null,{},record],featuredPlaylists:[record],artistPlaylists:[record],
            videos:[{id:"unsafe",title:"Unsafe",sourceUrl:"javascript:void(0)"}]
        }};
        compare(host.artistData.media.livePerformances.length, 1);
        compare(host.artistData.media.videos.length, 0);
        verify(item("vinylArtistLiveSection") === null);
        verify(item("vinylArtistFeaturedSection") === null);
        verify(item("vinylArtistArtistPlaylistsSection") === null);
        compare(item("vinylArtistRowIndexSidebar").automationRows, "");
        host.artistData.open("Uncatalogued artist");
        compare(host.artistData.media.livePerformances.length, 0);
        verify(item("vinylArtistLiveSection") === null);
    }
    function test_libraryProjectsExistingProfileWithoutCrossArtistLeak() {
        var album = host.artistData.record.releases[0].album;
        var catalogue = Object.assign({}, host.worldData.catalogue);
        catalogue.profiles = {new:{saved:[album.id,"unknown-id"],history:[]}};
        host.worldData.catalogue = catalogue;
        compare(host.artistData.libraryReleases.length, 1);
        compare(host.artistData.libraryReleases[0].album.id, album.id);
        verify(item("vinylArtistLibrarySection") === null);
        host.artistData.open("Uncatalogued artist");
        compare(host.artistData.libraryReleases.length, 0);
    }
    function test_nativeGridAndNarrowSongLayout() {
        host.artistData.open("Eminem");
        host.width = 960;
        wait(50);
        verify(!item("vinylArtistRowIndexSidebar").visible);
        compare(item("vinylArtistPanel_albums").width, host.artistScroll.bodyWidth);
        verify(item("vinylArtistDiscographyCard_0").width < host.artistScroll.bodyWidth);
        item("vinylArtistTab_singles").activate();
        compare(host.artistData.activeTab, "singles");
        compare(item("vinylArtistSinglesRow_0").height, 62);
        verify(item("vinylArtistSinglesAlbum_0").width > 150);
        item("vinylArtistTab_albums").activate();
        compare(host.artistData.activeTab, "albums");
    }
    function test_provenanceAndUnknownMetricsAreNotPromoted() {
        var source = {name:"Artist",provider:"Catalogue",sourceUrl:"https://example.test/artist",
            monthlyListeners:999, biography:"Unsourced", videos:[
                {id:"v",title:"Video",sourceUrl:"https://example.test/video",views:123},
                {id:"v",title:"Duplicate",sourceUrl:"https://example.test/video"}],
            relatedArtists:{title:"Not an array"}};
        var result = Catalogue.mediaForArtist({Artist:source}, "Artist");
        compare(result.videos.length, 1);
        compare(result.videos[0].views, undefined);
        compare(result.monthlyListeners, undefined);
        compare(result.biography, undefined);
        compare(result.relatedArtists.length, 0);
        source.videos.push({id:"__proto__",title:"Opaque provider ID",sourceUrl:"https://example.test/opaque"});
        source.videos.push({id:"bad-title",title:{name:"Not text"},sourceUrl:"https://example.test/bad"});
        result = Catalogue.mediaForArtist({Artist:source}, "Artist");
        compare(result.videos.length, 2);
        compare(result.videos[1].id, "__proto__");
        compare(Catalogue.mediaForArtist({Other:source}, "Other").videos.length, 0);
        source.sourceUrl = "file:///private";
        compare(Catalogue.mediaForArtist({Artist:source}, "Artist").videos.length, 0);
        compare(Catalogue.releaseCards([{id:"x",title:"Undated"}])[0].subtitle, "Album");
    }
    function test_keyboardScrollingAndFocusReveal() {
        host.artistData.open("Eminem");
        host.artistScroll.contentY = 0;
        host.artistScroll.forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(host.artistScroll.contentY, 72);
        keyClick(Qt.Key_PageDown);
        verify(host.artistScroll.contentY > 72);
        keyClick(Qt.Key_PageUp);
        fuzzyCompare(host.artistScroll.contentY, 72, 0.1);
        item("vinylArtistTab_videos").activate();
        wait(0);
        var card = item("vinylArtistVideoCard_0");
        card.forceActiveFocus();
        verify(host.artistScroll.contentY > 72);
        waitForRendering(host.contentItem);
        mouseClick(card, card.width / 2, card.artHeight / 2);
        tryCompare(item("vinylArtistMediaSource"), "visible", true);
        compare(item("vinylArtistMediaSource").accessibleDescription, host.artistData.media.videos[0].sourceUrl);
        keyClick(Qt.Key_Escape);
        tryVerify(function() { var source = item("vinylArtistMediaSource"); return !source || !source.visible; });
    }
    function test_nativeTabsHaveDataGatedContents() {
        host.artistData.open("Eminem");
        // Approved tab destination supersedes the earlier sidebar specification.
        verify(!item("vinylArtistRowIndexSidebar").visible);
        verify(!item("vinylArtistSections").visible);
        var tabs = ["albums", "singles", "videos", "live"];
        for (var i = 0; i < tabs.length; ++i) verify(item("vinylArtistTab_" + tabs[i]).visible);
        compare(host.artistData.allDiscographyCards.length, 27);
        compare(host.artistData.singlesRows.length, 43);
        compare(host.artistData.media.videos.length, 6);
        compare(host.artistData.liveCards.length, 3);
        verify(item("vinylArtistAbout") === null);
        verify(item("vinylArtistHistory_0") === null);
        host.artistData.open("Joni Mitchell");
        compare(host.artistData.discographyCards.length, 1);
        compare(host.artistData.media.videos.length, 0);
    }
    function test_videoDetailReturnsToFocusedTab() {
        host.artistData.open("Eminem");
        var playing = host.pageData.playingEditionId;
        item("vinylArtistTab_videos").activate();
        tryCompare(host.artistData, "activeTab", "videos");
        wait(0);
        var before = host.artistScroll.contentY;
        var card = item("vinylArtistVideoCard_0");
        card.forceActiveFocus();
        card.activate();
        tryCompare(item("vinylArtistMediaSource"), "visible", true);
        keyClick(Qt.Key_Escape);
        tryCompare(card, "activeFocus", true);
        compare(host.artistData.activeTab, "videos");
        compare(host.pageData.playingEditionId, playing);
        verify(host.artistScroll.contentY >= before);
    }
    function test_popularSongsRequireProviderRecords() {
        host.artistData.open("Eminem");
        compare(host.artistData.popularSource, "YouTube Music");
        verify(host.artistData.popularSongs.length > 0);
        compare(host.artistData.popularSongs[0].track.title, "Mockingbird");
        compare(host.artistData.popularSongs[1].track.title, "Without Me");
        compare(host.artistData.popularSongs.length,
                host.artistData.popularSongs.filter(function(s) { return !!s.album && !!s.track; }).length);
        host.artistData.open("Joni Mitchell");
        compare(host.artistData.popularSongs.length, 0);
        item("vinylArtistTab_singles").activate();
        var expected = host.artistData.singlesRows[0].song;
        verify(expected !== null);
        item("vinylArtistSinglesRow_0").activate();
        compare(host.pageData.playingTrack.title, expected.track.title);
    }
    function test_headerAndTabsStayPinned() {
        host.artistData.open("Eminem");
        var page = host.artistScroll;
        page.contentY = 640;
        var title = item("vinylArtistTitleBar");
        var tabs = item("vinylArtistTabBar");
        compare(title.y, page.contentY);
        compare(tabs.y, page.contentY + title.height);
        verify(title.solid);
        verify(!item("vinylArtistRowIndexSidebar").visible);
        item("vinylArtistTab_videos").activate();
        wait(0);
        compare(host.artistData.activeTab, "videos");
        verify(item("vinylArtistPanel_videos").visible);
        verify(page.contentY >= 0 && page.contentY <= page.contentHeight - page.height);
    }
    function test_eachTabOpensItsOwnCategory() {
        host.artistData.open("Eminem");
        var tabs = ["albums", "singles", "videos", "live"];
        for (var i = 0; i < tabs.length; ++i) {
            item("vinylArtistTab_" + tabs[i]).activate();
            compare(host.artistData.activeTab, tabs[i]);
            for (var j = 0; j < tabs.length; ++j)
                compare(item("vinylArtistPanel_" + tabs[j]).visible, i === j);
        }
    }
    function test_relatedArtistBackRestoresOriginalTab() {
        host.artistData.open("Eminem");
        item("vinylArtistTab_videos").activate();
        var original = host.artistData.record.name;
        var next = host.artistData.media.relatedArtists[0].title;
        host.openArtist(next);
        compare(host.artistData.record.name, next);
        compare(host.artistData.activeTab, "albums");
        var back = item("vinylArtistHeaderBack");
        mouseClick(back, back.width / 2, back.height / 2);
        compare(host.artistData.record.name, original);
        compare(host.artistData.activeTab, "videos");
    }
    function test_singlesUsesApprovedCompactRows() {
        host.artistData.open("Eminem");
        item("vinylArtistTab_singles").activate();
        compare(host.artistData.activeTab, "singles");
        var row = item("vinylArtistSinglesRow_0");
        verify(row !== null, "Approved native Singles uses compact numbered artwork-backed rows");
        compare(row.height, 62);
        row.activate();
        compare(host.artistData.queuedSongIndex, 0);
        compare(item("vinylArtistHudTitle").text, host.artistData.singlesRows[0].title);
    }
    function test_headerBackFromTabReturnsWorld() {
        host.page = "world";
        host.openArtist("Eminem");
        item("vinylArtistTab_videos").activate();
        var back = item("vinylArtistHeaderBack");
        mouseClick(back, back.width / 2, back.height / 2);
        compare(host.page, "world");
    }
    // ---- NEW TESTS: Approved Eminem HTML Parity & Theatre Dropdown Parity ----

    function test_eminemHtmlParityTabsAndDiscographyCounts() {
        host.artistData.open("Eminem");
        compare(host.page, "artist");
        // Second-layer destination: no first-layer world pills visible
        verify(item("vinylWorldPillsBar") !== null);
        compare(item("vinylWorldPillsBar").visible, false);
        verify(item("vinylArtistTitleBar") !== null);
        compare(item("vinylArtistTitleBar").solid, false);
        // 27 releases, 43 compact singles, 6 videos, 3 live items
        compare(host.artistData.allDiscographyCards.length, 27);
        compare(host.artistData.discographyCards.length, 27);
        compare(host.artistData.singlesRows.length, 43);
        compare(host.artistData.media.videos.length, 6);
        compare(host.artistData.liveCards.length, 3);
        verify(item("vinylArtistDiscographyCard_0") !== null);
        verify(item("vinylArtistDiscographyCard_26") !== null);
        // Sticky header and tab bar behavior when scrolled
        host.artistScroll.contentY = 640;
        compare(item("vinylArtistTitleBar").solid, true);
        compare(item("vinylArtistTabBar").y, host.artistScroll.contentY + item("vinylArtistTitleBar").height);
        host.artistScroll.contentY = 0;
    }

    function test_sharedReleasePickerKeyboardAndFocusRestoration() {
        host.artistData.open("Eminem");
        var trigger = item("vinylArtistReleasePickerAction");
        var menu = item("vinylArtistReleasePickerPopup");
        var menuFocus = item("vinylArtistReleasePickerList");
        verify(trigger !== null);
        verify(menu !== null);
        compare(menu.visible, false);
        trigger.forceActiveFocus();
        verify(trigger.activeFocus);

        // Open dropdown via Return key
        keyClick(Qt.Key_Return);
        compare(item("vinylArtistReleasePicker").open, true);
        compare(menu.visible, true);
        tryCompare(menuFocus, "activeFocus", true);
        compare(item("vinylArtistReleasePickerList").currentIndex, 0);

        // Down arrow moves to Studio albums (index 1), Return activates and restores focus to trigger
        keyClick(Qt.Key_Down);
        compare(item("vinylArtistReleasePickerList").currentIndex, 1);
        keyClick(Qt.Key_Return);
        compare(item("vinylArtistReleasePicker").open, false);
        compare(menu.visible, false);
        compare(host.artistData.categoryFilter, "Studio");
        compare(host.artistData.discographyCards.length, 12);
        tryCompare(trigger, "activeFocus", true);

        // Re-open, test Home/End/Up/Tab-trap and Escape dismissal restoring focus without altering selection
        keyClick(Qt.Key_Down);
        compare(item("vinylArtistReleasePicker").open, true);
        tryCompare(menuFocus, "activeFocus", true);
        compare(item("vinylArtistReleasePickerList").currentIndex, 1);
        keyClick(Qt.Key_End);
        compare(item("vinylArtistReleasePickerList").currentIndex, host.artistData.releaseCategories.length - 1);
        keyClick(Qt.Key_Home);
        compare(item("vinylArtistReleasePickerList").currentIndex, 0);
        keyClick(Qt.Key_Up);
        compare(item("vinylArtistReleasePickerList").currentIndex, host.artistData.releaseCategories.length - 1);
        keyClick(Qt.Key_Tab);
        compare(item("vinylArtistReleasePicker").open, true);
        compare(menuFocus.activeFocus, true);
        keyClick(Qt.Key_Escape);
        compare(item("vinylArtistReleasePicker").open, false);
        compare(host.artistData.categoryFilter, "Studio");
        tryCompare(trigger, "activeFocus", true);

        // Reset back to All releases (27)
        host.artistData.selectReleaseCategory("All");
        compare(host.artistData.discographyCards.length, 27);
    }

    function test_rotatingHeroAndSynchronizedWallpaperAcrossPhotoCounts() {
        host.artistData.open("Eminem");
        var hero = item("vinylArtistHero");
        var wallpaper = item("vinylArtistWallpaper");
        var timer = item("vinylArtistHeroTimer");
        verify(hero !== null);
        verify(wallpaper !== null);
        verify(timer !== null);

        // 4 eligible Eminem photos -> multi-rotate mode, synchronized layer sources
        compare(hero.eligiblePhotoCount, 4);
        compare(hero.rotationMode, "multi-rotate");
        compare(hero.slideshowEnabled, true);
        compare(timer.running, true);
        compare(hero.activeVisualLayer, 0);
        compare(wallpaper.artistActiveLayer, 0);
        compare(hero.layerASource, wallpaper.artistLayerASource);

        // Advance slideshow -> layer flips to 1 and both hero and wallpaper crossfade in lockstep
        hero.advanceSlideshow();
        compare(hero.slideshowIndex, 1);
        compare(hero.activeVisualLayer, 1);
        compare(wallpaper.artistActiveLayer, 1);
        compare(hero.layerBSource, wallpaper.artistLayerBSource);

        // Reduced motion pauses timer
        host.artistData.reducedMotion = true;
        compare(hero.slideshowEnabled, false);
        compare(timer.running, false);
        host.artistData.reducedMotion = false;
        compare(timer.running, true);

        // 2 eligible photos -> dual-crossfade mode
        host.artistData.overrideHeroPhotos = [
            {id: "p1", name: "Photo 1", cover: "artist-media/UCfM3zsQsOnfWNUppiycmBuw-hero.jpg"},
            {id: "p2", name: "Photo 2", cover: "artist-media/UCVlWr_LN9y80smEMr0KTBOA-hero.jpg"}
        ];
        compare(hero.eligiblePhotoCount, 2);
        compare(hero.rotationMode, "dual-crossfade");
        compare(timer.running, true);
        host.artistData.overrideHeroPhotos = null;

        // 1 eligible photo (Ilaiyaraaja) -> static mode, timer stopped
        host.artistData.open("Ilaiyaraaja");
        compare(hero.eligiblePhotoCount, 1);
        compare(hero.rotationMode, "static");
        compare(timer.running, false);

        // 0 eligible photos (Michael Jackson) -> fallback mode, timer stopped
        host.artistData.open("Michael Jackson");
        compare(hero.eligiblePhotoCount, 0);
        compare(hero.rotationMode, "fallback");
        compare(timer.running, false);
    }

    function test_compactSinglesListKeyboardNavigationAndHudQueue() {
        host.artistData.open("Eminem");
        item("vinylArtistTab_singles").activate();
        compare(host.artistData.activeTab, "singles");
        compare(item("vinylArtistPanel_singles").visible, true);
        var row0 = item("vinylArtistSinglesRow_0");
        var row1 = item("vinylArtistSinglesRow_1");
        var row42 = item("vinylArtistSinglesRow_42");
        verify(row0 !== null);
        verify(row1 !== null);
        verify(row42 !== null);
        compare(row0.height, 62);

        // Up/Down arrow navigation between compact rows
        row0.forceActiveFocus();
        verify(row0.activeFocus);
        keyClick(Qt.Key_Down);
        tryCompare(row1, "activeFocus", true);
        keyClick(Qt.Key_Up);
        tryCompare(row0, "activeFocus", true);

        // Enter/activate queues track and reveals HUD preview player
        compare(item("vinylArtistHudPlayer").visible, false);
        row0.activate();
        compare(host.artistData.queuedSongIndex, 0);
        compare(host.artistData.demoPlaying, true);
        compare(item("vinylArtistSinglesQueued_0").visible, true);
        compare(item("vinylArtistHudPlayer").visible, true);
        compare(item("vinylArtistHudTitle").text, "Houdini");
    }

    function test_videosLiveTabsAndArtistScopedSearch() {
        host.artistData.open("Eminem");
        var tabAlbums = item("vinylArtistTab_albums");
        var tabSingles = item("vinylArtistTab_singles");
        var tabVideos = item("vinylArtistTab_videos");
        var tabLive = item("vinylArtistTab_live");
        tabAlbums.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(host.artistData.activeTab, "singles");
        tryCompare(tabSingles, "activeFocus", true);
        keyClick(Qt.Key_End);
        compare(host.artistData.activeTab, "live");
        tryCompare(tabLive, "activeFocus", true);
        verify(item("vinylArtistLiveCard_0") !== null);
        verify(item("vinylArtistLiveCard_2") !== null);
        keyClick(Qt.Key_Left);
        compare(host.artistData.activeTab, "videos");
        tryCompare(tabVideos, "activeFocus", true);
        verify(item("vinylArtistVideoCard_0") !== null);
        verify(item("vinylArtistVideoCard_5") !== null);

        // Artist-scoped Search modal & Escape focus restoration
        var searchBtn = item("vinylArtistSearchButton");
        searchBtn.forceActiveFocus();
        searchBtn.activate();
        var searchDialog = item("vinylArtistSearchDialog");
        verify(searchDialog.visible);
        var results = host.artistData.search("Mockingbird");
        verify(results.length >= 2);
        keyClick(Qt.Key_Escape);
        tryCompare(searchDialog, "visible", false);
        tryCompare(searchBtn, "activeFocus", true);
    }

    function test_captureStates() {
        if (typeof VinylCaptureDirectory === "undefined" || !VinylCaptureDirectory)
            skip("Set VINYL_CAPTURE_DIR for visual captures");
        var dimensions = [[1280,720], [1600,900], [1920,1080]];
        var scenes = ["hero", "albums", "dropdown", "singles", "videos", "live"];
        for (var size = 0; size < dimensions.length; ++size) {
            host.width = dimensions[size][0];
            host.height = dimensions[size][1];
            host.artistData.open("Eminem");
            host.artistData.reducedMotion = true;
            tryCompare(host.artistScroll.heroItem, "portraitReady", true);
            for (var state = 0; state < scenes.length; ++state) {
                var scene = scenes[state];
                findChild(host, "vinylArtistReleasePicker").closePopup(false);
                host.artistData.activeTab = ["singles", "videos", "live"].indexOf(scene) >= 0 ? scene : "albums";
                host.artistScroll.contentY = scene === "hero" ? 0 : host.artistScroll.heroItem.height - 110;
                if (scene === "dropdown") item("vinylArtistReleasePickerAction").activate();
                if (scene === "singles") item("vinylArtistSinglesRow_0").activate();
                else host.artistData.queuedSongIndex = -1;
                host.requestUpdate();
                wait(600);
                waitForRendering(host.contentItem);
                var captured = false;
                var file = VinylCaptureDirectory + "/artist-native-" + dimensions[size][0] + "-" + scene + ".png";
                verify(host.contentItem.grabToImage(function(result) {
                    captured = result.saveToFile(file);
                }, Qt.size(host.width, host.height)));
                tryVerify(function() { return captured; });
            }
        }
    }
}
