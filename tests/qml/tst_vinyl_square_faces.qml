import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylSquareFaces"
    when: windowShown
    visible: true
    width: 800; height: 700

    readonly property url cover: Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg")
    QtObject { id: playback; property var playingTrack: null }
    QtObject {
        id: world
        property QtObject session: playback
        property bool pinned: false
        property int activations: 0
        function artwork(cover) { return cover || ""; }
        function activate(item) { activations++; }
        function pinCrateItem(item, pinned) { world.pinned = pinned; }
        function isCratePinned(item) { return pinned; }
    }
    Component {
        id: factory
        Vinyl.VinylItemCard { objectName: "testCard"; x: 40; y: 40; worldData: world }
    }
    function init() {
        playback.playingTrack = null;
        world.pinned = false;
        world.activations = 0;
        mouseMove(test, width - 2, height - 2);
        test.forceActiveFocus();
    }
    function make(kind, props) {
        const card = createTemporaryObject(factory, test, Object.assign({item:{
            id: "fixture", kind:kind, title:"Fixture title", artist:"Fixture artist", year:1977,
            cover:cover, artworkCovers:[cover,cover,cover,cover]
        }}, props || {}));
        verify(card);
        return card;
    }
    function painted(node) {
        if (!node.visible || node.opacity === 0) return 0;
        let count = node instanceof Rectangle || node instanceof Image
            || node instanceof BorderImage || node instanceof Text ? 1 : 0;
        for (const child of node.children) count += painted(child);
        return count;
    }
    function test_restingFamily_data() {
        return [
            {tag:"song", kind:"songs", pieces:4, detail:"Fixture artist"},
            {tag:"album", kind:"albums", pieces:4, detail:"Fixture artist · 1977"},
            {tag:"artist", kind:"artists", pieces:5, detail:"Artist"},
            {tag:"station", kind:"radio", pieces:4, detail:"((•))  Personal radio"},
            {tag:"mix", kind:"mixes", pieces:7, detail:"Made for you"}
        ];
    }
    function test_restingFamily(data) {
        const card = make(data.kind);
        compare(card.width, 196);
        compare(card.height, 251);
        const art = findChild(card, "testCard_cover");
        compare([art.x, art.y, art.width, art.height], [8,8,180,180]);
        const title = findChild(card, "testCard_captionTitle");
        compare(title.font.pixelSize, 17);
        compare(title.font.family, "Fraunces");
        compare(card.captionTop, 195);
        compare(findChild(card, "testCard_captionDetail").text, data.detail);
        const artwork = findChild(art, "vinylArtworkImage");
        tryCompare(artwork, "visible", true);
        compare(artwork.cache, true);
        compare(artwork.asynchronous, true);
        compare(artwork.sourceSize.width, Math.ceil(artwork.width * card.Screen.devicePixelRatio));
        if (data.kind === "artists") {
            const ring = findChild(art, "vinylArtistRing");
            verify(ring.visible);
            compare(ring.width, ring.height);
        }
        tryCompare(card, "hovered", false);
        tryVerify(() => painted(card) === data.pieces);
        compare(findChild(card, "vinylTileHint").item, null);
        compare(findChild(card, "vinylPlayingRail").item, null);
        world.pinned = true;
        compare(painted(card), data.pieces, "A pinned resting tile must stay within budget");
    }
    function test_hintAndActions_data() {
        return test_restingFamily_data();
    }
    function test_hintAndActions(data) {
        const card = make(data.kind);
        const tile = findChild(card, "testCard_tile");
        const hint = findChild(card, "vinylTileHint");
        mouseMove(card, 30, 30);
        tryVerify(() => hint.item !== null);
        tryCompare(hint.item, "opacity", 1);
        tryCompare(tile, "scale", 1.015);
        compare(hint.item.children[0].text, card.opensPage ? "→" : "▶");
        mouseClick(hint);
        compare(world.activations, 1, "The footer hint shares the primary card action");
        mouseMove(test, 780, 680);
        tryCompare(hint, "item", null);
        card.reducedMotion = true;
        card.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(() => hint.item !== null);
        compare(hint.item.opacity, 1);
        compare(tile.scale, 1);
        compare(tile.border.color, "#f0c44a");
        keyClick(Qt.Key_Escape);
        tryCompare(hint, "item", null);
        keyClick(Qt.Key_Return);
        compare(world.activations, 2);
    }
    function test_playingSong() {
        const card = make("songs");
        const title = findChild(card, "testCard_captionTitle");
        const rail = findChild(card, "vinylPlayingRail");
        playback.playingTrack = {id:"fixture"};
        compare(title.color, "#f0c44a");
        compare(rail.height, 2);
        verify(rail.item.visible);
        compare(rail.item.color, "#52555e");
        compare(rail.item.children[0].color, "#f0c44a");
        compare(rail.item.children[0].width, rail.width * 0.37);
        tryCompare(findChild(card,"vinylArtworkImage"), "visible", true);
        compare(painted(card), 6);
        mouseMove(card, 30, 30);
        tryCompare(card, "hovered", true);
        const hint = findChild(card, "vinylTileHint");
        compare(hint.item, null);
        playback.playingTrack = {id:"another"};
        compare(rail.item, null);
        compare(title.color, "#f7f7f5");
        tryVerify(() => hint.item !== null);
    }
}
