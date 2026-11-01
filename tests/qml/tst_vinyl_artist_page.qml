import QtQuick
import QtTest
import "../../qml/vinyl/VinylArtistCatalogue.js" as Catalogue

TestCase {
    name: "VinylArtistPage"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem, name); }
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
        item("vinylArtistAlbum_0").activate();
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
        item("vinylArtistAlbum_0").activate();
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
        var expected = host.artistData.songs[0];
        item("vinylArtistSong_0").activate();
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
        item("vinylSongBack").activate();
        compare(host.page, "artist");
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
        item("vinylArtistFilter_track").activate();
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
        item("vinylArtistSeeAll_albums").activate();
        compare(host.artistScroll.selectedCategory, "albums");
        var selected = host.artistData.albumCards[0].album;
        item("vinylArtistFullCategory_0").activate();
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
        host.artistScroll.contentY = 180;
        item("vinylArtistSeeAll_singles").activate();
        compare(host.artistScroll.selectedCategory, "singles");
        var first = host.artistData.singleCards[0].album;
        item("vinylArtistFullCategory_0").activate();
        compare(host.page, "album");
        compare(host.pageData.shownEdition.name, first.title);
        item("vinylAlbumBack").activate();
        compare(host.page, "artist");
        compare(host.artistScroll.selectedCategory, "singles");
        item("vinylArtistFullBack").activate();
        tryCompare(host.artistScroll, "contentY", 180);
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
        item("vinylArtistVideo_0").activate();
        tryVerify(function() { return item("vinylArtistMediaSource") !== null; });
        compare(item("vinylArtistMediaSource").accessibleDescription, host.artistData.media.videos[0].sourceUrl);
        compare(host.pageData.playingEditionId, playing);
        keyClick(Qt.Key_Escape);
        tryVerify(function() { var source = item("vinylArtistMediaSource"); return !source || !source.visible; });
        var related = host.artistData.media.relatedArtists[0].title;
        item("vinylArtistRelated_0").activate();
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
    function test_shelfExpansionAndNarrowSongLayout() {
        host.artistData.open("Eminem");
        host.width = 960;
        wait(50);
        var sidebar = item("vinylArtistRowIndexSidebar");
        var albums = item("vinylArtistAlbumsSection");
        verify(albums !== null);
        compare(albums.width, host.artistScroll.bodyWidth - sidebar.contentLeft);
        var rail = item("vinylArtistAlbumRail");
        verify(rail.contentWidth > rail.width);
        var songs = item("vinylArtistSongsSection");
        compare(songs.width, albums.width);
        verify(item("vinylArtistSongAlbum_0").width > 150);
        item("vinylArtistSeeAll_albums").activate();
        compare(host.artistScroll.selectedCategory, "albums");
        verify(item("vinylArtistFullCategory_0").width < host.artistScroll.bodyWidth);
        item("vinylArtistFullBack").activate();
        tryCompare(host.artistScroll, "selectedCategory", "");
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
        compare(host.artistScroll.contentY, 64);
        keyClick(Qt.Key_PageDown);
        verify(host.artistScroll.contentY > 64);
        keyClick(Qt.Key_PageUp);
        fuzzyCompare(host.artistScroll.contentY, 64, 0.1);
        var card = item("vinylArtistVideo_0");
        card.forceActiveFocus();
        verify(host.artistScroll.contentY > 64);
        mouseClick(card, card.width / 2, card.artHeight / 2);
        tryCompare(item("vinylArtistMediaSource"), "visible", true);
        compare(item("vinylArtistMediaSource").accessibleDescription, host.artistData.media.videos[0].sourceUrl);
        keyClick(Qt.Key_Escape);
        tryVerify(function() { var source = item("vinylArtistMediaSource"); return !source || !source.visible; });
    }
    function test_classicSidebarHasOnlySourcedCategories() {
        host.artistData.open("Eminem");
        var sidebar = item("vinylArtistRowIndexSidebar");
        verify(sidebar !== null, "Use Colosseum's existing RowIndexSidebar");
        compare(sidebar.automationRows, ["Popular songs", "Albums", "Singles & EPs", "Videos", "Related artists"].join(String.fromCharCode(10)));
        verify(sidebar.rows.every(function(row) {
            return row.iconSource && row.iconSource.indexOf("data:image/svg+xml,") === 0;
        }), "Each category uses Colosseum's sourced sidebar icon contract");
        compare(sidebar.collapsed, true);
        verify(item("vinylArtistRowIndexRow_songsAction") !== null);
        verify(item("vinylArtistSeeAll_songs") !== null);
        verify(item("vinylArtistSeeAll_albums") !== null);
        verify(item("vinylArtistSeeAll_singles") !== null);
        verify(item("vinylArtistSeeAll_videos") !== null);
        verify(item("vinylArtistSeeAll_related") !== null);
        verify(item("vinylArtistAbout") === null);
        verify(item("vinylArtistHistory_0") === null);
        host.artistData.open("Joni Mitchell");
        compare(sidebar.automationRows, ["Songs", "Albums"].join(String.fromCharCode(10)));
    }
    function test_seeAllOpensFocusedCategoryAndRestoresScroll() {
        host.artistData.open("Eminem");
        var playing = host.pageData.playingEditionId;
        host.artistScroll.contentY = 225;
        item("vinylArtistSeeAll_albums").activate();
        compare(host.artistScroll.selectedCategory, "albums");
        verify(item("vinylArtistFullCategory_0") !== null);
        verify(item("vinylArtistRowIndexSidebar").visible === false);
        item("vinylArtistFullBack").activate();
        compare(host.artistScroll.selectedCategory, "");
        tryCompare(host.artistScroll, "contentY", 225);
        compare(host.pageData.playingEditionId, playing);
        item("vinylArtistSeeAll_songs").activate();
        compare(host.artistScroll.selectedCategory, "songs");
        compare(item("vinylArtistDetailTitle").text, "All songs");
        item("vinylArtistFullBack").activate();
        item("vinylArtistSeeAll_videos").activate();
        compare(host.artistScroll.selectedCategory, "videos");
        item("vinylArtistFullCategory_0").activate();
        tryVerify(function() { return item("vinylArtistMediaSource") !== null; });
        keyClick(Qt.Key_Escape);
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
        verify(!item("vinylArtistSongsSection").visible);
        verify(item("vinylArtistUnrankedSongsSection").visible);
        compare(item("vinylArtistUnrankedSongsSection").title, "Songs");
        var expected = host.artistData.songOrder[0];
        item("vinylArtistUnrankedSong_0").activate();
        compare(host.pageData.playingTrack.title, expected.track.title);
    }
    function test_sidebarIsPinnedAndRailCanScrollIndependently() {
        host.artistData.open("Eminem");
        var sidebar = item("vinylArtistRowIndexSidebar");
        var initialWidth = item("vinylArtistAlbumsSection").width;
        item("vinylArtistRowIndexCollapseAction").activate();
        compare(sidebar.collapsed, false);
        tryVerify(function() { return item("vinylArtistAlbumsSection").width < initialWidth - 150; });
        item("vinylArtistRowIndexRow_videosAction").activate();
        verify(host.artistScroll.contentY > 0);
        verify(item("vinylArtistVideosSection").visible);
        var rail = item("vinylArtistAlbumRail");
        verify(rail !== null);
        verify(rail.contentWidth > rail.width);
        var before = host.artistScroll.contentY;
        rail.contentX = 80;
        compare(host.artistScroll.contentY, before);
    }
    function test_eachSeeAllOpensItsOwnCategory() {
        host.artistData.open("Eminem");
        var cases = [
            ["songs", "All songs"],
            ["albums", "Albums"],
            ["singles", "Singles & EPs"],
            ["videos", "Videos"],
            ["related", "Related artists"]
        ];
        for (var i = 0; i < cases.length; ++i) {
            item("vinylArtistSeeAll_" + cases[i][0]).activate();
            compare(host.artistScroll.selectedCategory, cases[i][0]);
            compare(item("vinylArtistDetailTitle").text, cases[i][1]);
            verify(item("vinylArtistFullCategory_0") !== null);
            item("vinylArtistFullBack").activate();
            tryCompare(host.artistScroll, "selectedCategory", "");
        }
    }
    function test_relatedArtistDetailBackRestoresOriginalCategory() {
        host.artistData.open("Eminem");
        item("vinylArtistSeeAll_related").activate();
        var original = host.artistData.record.name;
        var next = host.artistData.media.relatedArtists[0].title;
        item("vinylArtistFullCategory_0").activate();
        compare(host.artistData.record.name, next);
        compare(host.artistScroll.selectedCategory, "");
        host.artistBack();
        compare(host.artistData.record.name, original);
        compare(host.artistScroll.selectedCategory, "related");
        item("vinylArtistFullBack").activate();
        tryCompare(host.artistScroll, "selectedCategory", "");
    }
    function test_seeAllSongsUsesColosseumCardsNotProviderRows() {
        host.artistData.open("Eminem");
        item("vinylArtistSeeAll_songs").activate();
        compare(host.artistScroll.selectedCategory, "songs");
        verify(item("vinylArtistFullSong_0") === null);
        var tile = item("vinylArtistFullCategory_0");
        verify(tile !== null, "All songs uses native Colosseum artwork cards");
        var expected = host.artistData.songOrder[0];
        tile.activate();
        compare(host.pageData.playingTrack.title, expected.track.title);
    }
    function test_globalBackFromSeeAllReturnsOverview() {
        host.artistData.open("Eminem");
        item("vinylArtistSeeAll_videos").activate();
        item("vinylAlbumBack").activate();
        compare(host.page, "artist");
        compare(host.artistScroll.selectedCategory, "");
        verify(item("vinylArtistVideosSection").visible);
    }
    function test_captureStates() {
        if (typeof VinylCaptureDirectory === "undefined" || !VinylCaptureDirectory)
            skip("Set VINYL_CAPTURE_DIR for visual captures");
        var cases = [
            {name:"Eminem",width:1280,height:720,tag:"artist-sidebar-1280"},
            {name:"Eminem",width:1600,height:900,tag:"artist-sidebar-1600"},
            {name:"Eminem",width:960,height:720,tag:"artist-sidebar-960"},
            {name:"Eminem",width:1280,height:720,section:"songs",tag:"artist-sidebar-songs"},
            {name:"Eminem",width:1280,height:720,section:"albums",tag:"artist-sidebar-albums"},
            {name:"Eminem",width:1280,height:720,section:"singles",tag:"artist-sidebar-singles"},
            {name:"Eminem",width:1280,height:720,section:"videos",tag:"artist-sidebar-videos"},
            {name:"Eminem",width:1280,height:720,section:"related",tag:"artist-sidebar-related"},
            {name:"Eminem",width:960,height:720,section:"albums",tag:"artist-sidebar-albums-960"},
            {name:"Eminem",width:960,height:720,section:"videos",tag:"artist-sidebar-videos-960"},
            {name:"Eminem",width:1280,height:720,detail:"albums",tag:"artist-sidebar-all-albums"},
            {name:"Eminem",width:960,height:720,detail:"songs",tag:"artist-sidebar-all-songs-960"},
            {name:"Ilaiyaraaja",width:960,height:720,tag:"artist-sidebar-ilaiyaraaja-960"},
            {name:"S. P. Balasubrahmanyam",width:1280,height:720,tag:"artist-sidebar-credits"},
            {name:"Joni Mitchell",width:960,height:720,tag:"artist-sidebar-thin"}
        ];
        for (var i = 0; i < cases.length; ++i) {
            var data = cases[i];
            host.close(); host.destroy();
            var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
            host = component.createObject(null, {width: data.width, height: data.height,
                flags: Qt.Window | Qt.FramelessWindowHint});
            verify(host !== null);
            host.pageData.frozen = true;
            host.artistData.open(data.name);
            tryCompare(host, "ready", true);
            tryCompare(host, "width", data.width);
            tryCompare(host, "height", data.height);
            host.artistScroll.contentY = 0;
            if (data.section)
                item("vinylArtistRowIndexRow_" + data.section + "Action").activate();
            if (data.detail)
                item("vinylArtistSeeAll_" + data.detail).activate();
            for (var cover = 0; cover < item("vinylArtistAlbumsSection").shownRecords.length; ++cover)
                tryCompare(item("vinylArtistAlbumArt_" + cover), "ready", true);
            if (host.artistData.media.portrait.cover)
                tryCompare(item("vinylArtistPortrait"), "status", Image.Ready);
            for (var mediaIndex = 0; mediaIndex < item("vinylArtistVideosSection").shownRecords.length; ++mediaIndex)
                tryCompare(item("vinylArtistVideoArt_" + mediaIndex), "ready", true);
            if (data.detail && data.detail !== "songs") {
                for (var full = 0; full < host.artistData.albumCards.length && full < 3; ++full)
                    tryCompare(item("vinylArtistFullArt_" + full), "ready", true);
            }
            host.requestUpdate();
            wait(600); // Allow the shared poster primitive's 280 ms ready fade to finish.
            waitForRendering(host.contentItem);
            var captured = false;
            verify(host.contentItem.grabToImage(function(result) {
                captured = result.saveToFile(VinylCaptureDirectory + "/" + data.tag + ".png");
            }, Qt.size(data.width, data.height)));
            tryVerify(function() { return captured; });
        }
    }
}
