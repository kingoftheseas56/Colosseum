import QtQuick
import QtTest
import "../../qml" as House
import "../../qml/vinyl" as Vinyl
TestCase {
    id: test
    name: "VinylHero"
    when: windowShown
    visible: true
    width: 1240
    height: 760
    property var hero: null
    property bool replaceHidden: false
    Component { id: heroFactory; Vinyl.VinylHero {} }
    QtObject {
        id: artSource
        property var calls: []
        signal completed(string lane, int id, var data, string error)
        function request(lane, operation, value, payload) {
            var id = calls.length + 1;
            calls.push({lane, id, operation, value, payload});
            return id;
        }
        function cancel(lane) {}
    }
    QtObject {
        id: session
        property var source: artSource
        property bool profileActive: true
        property int profileGeneration: 1
    }
    Component { id: houseFactory; House.FeaturedCarousel {} }
    SignalSpy { id: playSpy; target: test.hero; signalName: "playRequested" }
    SignalSpy { id: radioSpy; target: test.hero; signalName: "radioRequested" }
    SignalSpy { id: openSpy; target: test.hero; signalName: "openRequested" }
    SignalSpy { id: saveSpy; target: test.hero; signalName: "saveRequested" }
    SignalSpy { id: hideSpy; target: test.hero; signalName: "hideRequested" }
    SignalSpy { id: houseSpy; signalName: "primaryClicked" }
    Connections {
        target: test.hero
        function onHideRequested(item) {
            if (!test.replaceHidden) return;
            var next = test.hero.slides.slice();
            next[3] = Object.assign({}, next[3], {item:test.card("albums", "for-you-replacement")});
            test.hero.slides = next;
        }
    }
    function card(kind, id) {
        return {kind:kind, id:id, title:"Fixture " + id, artist:"Fixture artist",
            subtitle:"Fixture metadata", reason:"Because you liked this artist",
            cover:Qt.resolvedUrl("../../qml/vinyl/assets/record-ring.svg")};
    }
    function groups() {
        return ["topAlbum", "topSong", "topArtist", "forYou"].map(function(kind, i) {
            var type = ["albums", "songs", "artists", "albums"][i];
            var side = [];
            for (var n = 0; n < 4; ++n) {
                var row = test.card(i === 3 ? ["songs", "albums", "artists", "albums"][n] : type, kind + "-side-" + n);
                row.rank = n + 2;
                if (i === 3) row.seedArtistRef = {name:"Picked artist"};
                side.push(row);
            }
            return {kind:kind, item:test.card(type, kind + "-main"), sideItems:side,
                kicker:kind === "forYou" ? "For you · picked today" : "#1 this week", week:"2026-W46"};
        });
    }
    function init() {
        replaceHidden = false;
        mouseMove(test, width - 2, height - 2);
        hero = heroFactory.createObject(test, {x:20, y:20, width:1200, slides:groups()});
        verify(hero !== null);
        verify(waitForRendering(hero));
        playSpy.clear(); radioSpy.clear(); openSpy.clear(); saveSpy.clear(); hideSpy.clear();
    }
    function cleanup() {
        if (hero) { hero.destroy(); wait(0); hero = null; }
    }
    function settle() {
        tryVerify(function() {
            var slide = findChild(hero, "vinylHeroSlide");
            return slide && Math.abs(slide.mapToItem(hero, 0, 0).x) < 1;
        });
    }
    function jump(index) {
        var dot = findChild(hero, "vinylHeroDot_" + index);
        verify(dot !== null);
        mouseClick(dot);
        tryCompare(hero, "slideIndex", index);
        settle();
    }
    function click(name) {
        var action = findChild(hero, name);
        verify(action !== null, name);
        verify(action.visible && action.enabled);
        verify(waitForRendering(action));
        mouseClick(action);
    }
    function test_countOrderFourAndNoResume() {
        compare(hero.slideCount, 4);
        compare(hero.objectName, "vinylHero");
        compare(hero.height, 300);
        compare(JSON.stringify(hero.slides.map(function(s) { return s.kind; })),
                JSON.stringify(["topAlbum", "topSong", "topArtist", "forYou"]));
        for (var i = 0; i < 4; ++i) {
            jump(i);
            compare(hero.slideKind, hero.slides[i].kind);
            compare(hero.itemId, hero.slides[i].item.id);
            verify(hero.slideKind !== "resume");
        }
    }
    function test_sheetStoriesAndMissingMeta() {
        compare(hero.story({kind:"topAlbum"}),"The most-played album across the charts this week. Start it from the top.");
        compare(hero.story({kind:"topSong"}),"Top of the global song chart. Play it, or let it start a station that keeps going.");
        compare(hero.story({kind:"forYou",item:{seedArtistRef:{name:"Nujabes"}}}),"");
        compare(hero.story({kind:"forYou"}),"");
        jump(3);
        compare(findChild(hero,"vinylHeroStory").visible,false);
        hero.width=360;
        compare(findChild(hero,"vinylHeroStory").visible,false);
        compare(hero.meta({kind:"topArtist",item:{kind:"artists"}}),"");
        compare(hero.meta({kind:"topAlbum",item:{kind:"albums",artist:"Artist",year:2020,trackCount:10}}),"Artist · 2020 · 10 songs");
        compare(hero.meta({kind:"topSong",item:{kind:"songs",artist:"Artist",albumTitle:"Album"}}),"Artist · from Album");
    }
    function test_everySlideHasFourSideRowsAndForYouLabels() {
        for (var i = 0; i < 4; ++i) {
            jump(i);
            for (var row = 0; row < 4; ++row) {
                var tile = findChild(hero, "vinylHeroSideRow_" + row);
                verify(tile !== null && tile.visible);
                compare(tile.item.id, hero.slides[i].sideItems[row].id);
                if (i === 3) {
                    verify(tile.kindLabel.length > 0);
                    var subtitle=findChild(hero,"vinylHeroSideSubtitle_"+row);
                    compare(subtitle.text,row===2 ? "Artist" : tile.kindLabel+" · Fixture artist");
                }
            }
            compare(findChild(hero, "vinylHeroSideRow_4"), null);
        }
    }
    function test_chart_artists_gain_art_without_replacing_slide() {
        artSource.calls = [];
        var next = groups();
        var artist = next[2];
        var cover = String(artist.item.cover);
        for (var row of [artist.item].concat(artist.sideItems)) {
            row.cover = "";
            row.catalogueItem = {id:"lastfm:artists:" + row.id, kind:"artists", source:"lastfm",
                title:row.title, catalogueCard:true};
        }
        hero.slides = next;
        jump(2);
        var slide = findChild(hero, "vinylHeroSlide");
        var main = slide.children.find(child => child instanceof House.RoundedPosterImage);
        var side = [];
        for (var i = 0; i < 4; ++i) {
            var tile = findChild(hero, "vinylHeroSideRow_" + i);
            side.push(tile.children.find(child => child instanceof House.RoundedPosterImage));
        }
        for (var art of [main].concat(side)) compare(String(art.activeSource), "");
        if ("session" in hero) hero.session = session;
        tryVerify(() => artSource.calls.length > 0);
        var first = artSource.calls[0];
        compare(first.operation, "resolveCatalogueArt");
        compare(first.value, "hero");
        compare(first.payload.items.length, 5);
        artSource.completed(first.lane, first.id, {version:1, covers:{}, settled:[]}, "");
        tryVerify(() => artSource.calls.length > 1);
        var poll = artSource.calls[1];
        var ids = poll.payload.items.map(card => card.id), covers = {};
        for (var id of ids) covers[id] = cover;
        artSource.completed(poll.lane, poll.id, {version:1, covers:covers, settled:ids}, "");
        for (var image of [main].concat(side)) {
            compare(String(image.activeSource), cover);
            tryCompare(image, "ready", true);
        }
        compare(findChild(hero, "vinylHeroSlide"), slide);
        compare(hero.itemId, artist.item.id);
        compare(artist.item.cover, "", "art must not mutate the ranked card");
        session.profileGeneration++;
        compare(String(main.activeSource), "");
    }
    function test_actionsKeepTypedItemsAndDistinctRadioDispatch() {
        jump(0); click("vinylHeroPlay"); click("vinylHeroOpen"); click("vinylHeroSave");
        compare(playSpy.count, 1); compare(playSpy.signalArguments[0][0].id, "topAlbum-main");
        compare(openSpy.count, 1); compare(saveSpy.count, 1);
        compare(saveSpy.signalArguments[0][0].id, "topAlbum-main");
        jump(1); click("vinylHeroPlay"); click("vinylHeroRadio");
        compare(playSpy.count, 2); compare(radioSpy.count, 1);
        compare(radioSpy.signalArguments[0][0].id, "topSong-main");
        jump(2); click("vinylHeroRadio"); click("vinylHeroOpen");
        compare(radioSpy.count, 2); compare(openSpy.count, 2);
        compare(radioSpy.signalArguments[1][0].id, "topArtist-main");
        jump(3); click("vinylHeroPlay"); click("vinylHeroOpen");
        compare(playSpy.count, 3); compare(openSpy.count, 3);
        click("vinylHeroSideRow_0");
        compare(openSpy.signalArguments[3][0].id, "forYou-side-0");
    }
    function test_hideRequestsOwnerReplacementWithoutLocalHistory() {
        replaceHidden = true;
        jump(3);
        var oldId = hero.itemId;
        click("vinylHeroNotForMe");
        compare(hideSpy.count, 1);
        compare(hideSpy.signalArguments[0][0].id, oldId);
        tryCompare(hero, "itemId", "for-you-replacement");
        compare(hero.slideKind, "forYou");
        compare(hero.slideCount, 4);
        verify(hero.itemId !== oldId);
    }
    function laidOut() {
        var view = findChild(hero, "vinylHeroCarousel");
        for (var i = 1; i < view.count; ++i) {
            var a = view.itemAt(i - 1), b = view.itemAt(i);
            if (!a || !b || Math.abs(b.x - a.x - view.width) > 1) return false;
        }
        return true;
    }
    function test_republishKeepsSlidesLaidOutAndInPlace() {
        // Live Home republishes the hero while optional rows arrive, and
        // fullscreen toggles resize it. Neither may stack slides on top of each
        // other (blank or half-shifted hero) or jump back to the first slide.
        jump(1);
        var before = findChild(hero, "vinylHeroSlide");
        hero.slides = groups();                       // identical content
        wait(50);
        compare(hero.slideIndex, 1);
        verify(findChild(hero, "vinylHeroSlide") === before, "identical republish must not rebuild slides");
        // Hidden mid-slide (the loading gate / fullscreen flash) and resized.
        hero.slideIndex = 2; wait(60);
        hero.visible = false; hero.width = 1500; wait(150);
        hero.visible = true; wait(600);
        hero.slideIndex = 1; wait(600);
        var next = groups();
        next[1] = Object.assign({}, next[1], {kind:"topArtist"});   // same item, new group kind
        next[2] = Object.assign({}, next[2], {kind:"topSong"});
        hero.slides = next;
        tryVerify(laidOut, 3000, "slides must stay side by side after a republish");
        compare(hero.slideIndex, 1, "the shown item stays on screen when its group changes");
        compare(hero.itemId, "topSong-main");
        settle();
        hero.width = 1200; wait(100);
        jump(2);
        verify(laidOut());
    }
    function test_omittedGroupStaysOmitted() {
        hero.slides = groups().filter(function(s) { return s.kind !== "topSong"; });
        tryCompare(hero, "slideCount", 3);
        compare(JSON.stringify(hero.slides.map(function(s) { return s.kind; })),
                JSON.stringify(["topAlbum", "topArtist", "forYou"]));
    }
    function test_timerAdvancesAndHoverPauses() {
        var timer = findChild(hero, "vinylHeroTimer");
        verify(timer !== null);
        compare(timer.interval, 7000);
        mouseMove(test, width - 2, height - 2);
        tryCompare(timer, "running", true);
        mouseMove(hero, 60, 60);
        tryCompare(hero, "hovered", true);
        compare(timer.running, false);
        compare(hero.slideIndex, 0);
        mouseMove(test, width - 2, height - 2);
        tryCompare(hero, "hovered", false);
        tryCompare(timer, "running", true);
        tryCompare(hero, "slideIndex", 1, 8000);
    }
    function test_responsiveActionsAndSideRowsRemainInsideHero() {
        jump(3);
        for (var size of [360, 600, 1200]) {
            hero.width = size;
            tryCompare(hero, "height", size < 480 ? 440 : size < 760 ? 390 : 300);
            settle();
            for (var name of ["vinylHeroPlay", "vinylHeroOpen", "vinylHeroNotForMe", "vinylHeroSideRow_3", "vinylHeroDot_3"]) {
                var target = findChild(hero, name);
                verify(target !== null && target.visible);
                var at = target.mapToItem(hero, 0, 0);
                verify(at.x >= -1 && at.y >= -1 && at.x + target.width <= hero.width + 1
                    && at.y + target.height <= hero.height + 1, name + " at width " + size);
            }
        }
    }
    function test_dotsStayInsideAfterInitiallyUnallocatedWidth() {
        hero.width = -36;
        hero.slides = groups().filter(function(group) { return group.kind !== "topArtist"; });
        wait(0);
        hero.width = 1172;
        tryCompare(hero, "height", 300);
        settle();
        for (var i = 0; i < 3; ++i) {
            var dot = findChild(hero, "vinylHeroDot_" + i);
            verify(dot !== null);
            var at = dot.mapToItem(hero, 0, 0);
            verify(at.x >= 30 && at.x + dot.width <= hero.width,
                "Dot " + i + " lies outside Hero: x=" + at.x);
            verify(at.y >= 0 && at.y + dot.height <= hero.height);
        }
    }
    function test_houseDefaultDelegateRemainsAvailable() {
        var house = createTemporaryObject(houseFactory, test,
            {width:1000, height:250, slides:[{title:"House default", blurb:"Unchanged"}]});
        verify(house !== null);
        var label = findChild(house, "featuredPrimaryLabel_0");
        verify(label !== null);
        compare(label.text, "Read");
        compare(house.kicker, "Featured");
        compare(house.primaryLabel, "Read");
        compare(house.secondaryLabel, "Details");
        compare(house.slideDelegate, null);
        compare(house.dotAutomationPrefix, "");
        compare(house.dotsBottomMargin, 26);
        houseSpy.target = house;
        houseSpy.clear();
        var primary = findChild(house, "featuredPrimaryAction_0");
        verify(waitForRendering(primary));
        mouseClick(primary);
        compare(houseSpy.count, 1);
        compare(houseSpy.signalArguments[0][0], 0);
        houseSpy.target = null;
    }
    function test_houseDefaultDotsKeepRightMargin() {
        var house = createTemporaryObject(houseFactory, test, {
            width:1000, height:250, dotAutomationPrefix:"houseDot",
            slides:[{title:"First"}, {title:"Second"}]
        });
        verify(house !== null);
        verify(waitForRendering(house));
        var strip = findChild(house, "houseDot_0").parent.parent;
        compare(strip.x + strip.width, house.width - house.dotsRightMargin);
        house.width = 360;
        tryCompare(house, "dotsRightMargin", 16);
        compare(strip.x + strip.width, house.width - 16);
    }
}
