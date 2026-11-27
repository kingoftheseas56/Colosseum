import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl
import "../../qml" as House

TestCase {
    id: test
    name: "HouseCreationBudget"
    when: windowShown && faceFont.status === FontLoader.Ready
    width: 900; height: 700
    visible: true
    // Laptop 2: ~240 ms on the base and ~150 ms with deferred creation.
    readonly property int budgetMs: 190

    FontLoader { id: faceFont; source: "../../assets/fonts/Fraunces-Regular.ttf" }
    Vinyl.VinylPageData { id: playback; frozen: true }
    QtObject {
        id: world
        property QtObject session: null
        function artwork(cover) { return cover; }
        function setRecommendationFeedback() { return true; }
        function recommendationProfileGeneration() { return 0; }
        function recommendationArtistRef() { return null; }
        function pinCrateItem() {}
        function isCratePinned() { return false; }
    }
    Component { id: batchFactory; Item { x: -1000 } }
    Component { id: cardFactory; Vinyl.VinylItemCard { width: 196; worldData: world } }
    Component { id: rowFactory; Vinyl.VinylTrackRow { width: 600; pageData: playback } }
    Component { id: posterArtFactory; House.RoundedPosterImage { width: 196; height: 290 } }
    Component { id: posterFactory; House.CataloguePosterCard { width: 148; height: 275; visualProfile: "gallery" } }

    function medianCreation(component, props) {
        let batch = batchFactory.createObject(test);
        verify(component.createObject(batch, props[0]));
        wait(30);
        batch.destroy();
        wait(0);
        const samples = [];
        for (let trial = 0; trial < 5; ++trial) {
            batch = batchFactory.createObject(test);
            const start = Date.now();
            for (const entry of props) component.createObject(batch, entry);
            samples.push(Date.now() - start);
            compare(batch.children.length, 50);
            batch.destroy();
            wait(0);
        }
        samples.sort((a, b) => a - b);
        return {median: samples[2], samples: samples};
    }
    function test_cardFamilyAgainstPoster() {
        const cover = Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg");
        const posterArt = medianCreation(posterArtFactory, Array.from({length:50}, () => ({sources:[cover]})));
        const poster = medianCreation(posterFactory, Array.from({length:50}, (_, i) => ({item:{
            id:"film" + i, title:"Title " + i, year:2003, cover:String(cover)
        }})));
        const results = {posterArt:posterArt, posterCard:poster};
        for (const kind of ["songs", "albums", "artists", "radio", "mixes"]) {
            results[kind] = medianCreation(cardFactory, Array.from({length:50}, (_, i) => ({item:{
                id:kind + i, title:"Title " + i, artist:"Artist", year:2003,
                kind:kind, cover:cover, artworkCovers:kind === "mixes" ? [cover,cover,cover,cover] : []
            }})));
        }
        console.info("Creation (50 offscreen, warm caches, ms): " + JSON.stringify(results));
        // Use the ~90 ms reference ceiling, rather than asserting a noisy ratio
        // to another short measurement on arbitrary CI hardware.
        for (const kind of ["songs", "albums", "artists", "radio", "mixes"])
            verify(results[kind].median <= 90, kind + " took " + results[kind].median + " ms (budget: 90 ms)");
    }

    function test_creationBudget() {
        // Reuse the Thriller tracks from the HUD/album tests. Give each card
        // active Like, Hide and Pin badges, as on a Home recommendations shelf.
        const cards = [], rows = [];
        const recommendation = {recommendation: true, profileGeneration: 0};
        const cover = Qt.resolvedUrl("../../assets/icons/play.svg");
        for (let i = 0; i < 100; ++i) {
            const track = playback.tracks[i % playback.tracks.length];
            cards.push({recommendationRow: recommendation, item: {
                id: "benchmark-" + i, ref: {id: "benchmark-" + i},
                kind: "songs", title: track.title, artist: track.artist,
                albumTitle: playback.shownEdition.name, cover: cover
            }});
        }
        for (let i = 0; i < 60; ++i)
            rows.push({trackIndex: i % playback.tracks.length});

        // Warm component loading and text/asset caches, not the measured instances.
        const warm = batchFactory.createObject(test);
        const card = cardFactory.createObject(warm, cards[0]);
        const row = rowFactory.createObject(warm, rows[0]);
        verify(card && row);
        verify(findChild(card, "vinylRecommendationLike_benchmark-0").visible);
        verify(findChild(card, "vinylRecommendationHide_benchmark-0").visible);
        verify(findChild(card, "vinylPin_benchmark-0").visible);
        wait(0);
        warm.destroy();
        wait(0);

        const samples = [];
        for (let trial = 0; trial < 5; ++trial) {
            const batch = batchFactory.createObject(test);
            let start = Date.now();
            for (const props of cards)
                cardFactory.createObject(batch, props);
            const cardsMs = Date.now() - start;
            start = Date.now();
            for (const props of rows)
                rowFactory.createObject(batch, props);
            const rowsMs = Date.now() - start;
            compare(batch.children.length, 160);
            samples.push({cards: cardsMs, rows: rowsMs, total: cardsMs + rowsMs});
            batch.destroy();
            wait(0);
        }
        samples.sort((a, b) => a.total - b.total);
        console.info("Creation samples (100 cards + 60 rows, ms): " + JSON.stringify(samples));
        const median = samples[2].total;
        verify(median <= budgetMs, "Median creation took " + median + " ms (budget: " + budgetMs + " ms)");
    }
}
