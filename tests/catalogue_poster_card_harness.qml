// Visual-contract proof for the shared CataloguePosterCard (Catalogue Poster & Shelf Polish, Task 3).
// Two profiles: `classic` (unchanged for consumers not yet cleared) and `gallery` (the approved
// Theatre polish). Product-critical invariants held across BOTH: the IMDb rating is HIDDEN at rest,
// revealed ONLY on pointer hover, and keyboard focus never impersonates hover. Gallery adds a
// two-line title reserve, a hover source attribution, and NO centered play ring.
// NEVER throw offscreen: collect fails, print the OK marker only when clean, single Qt.exit.
import QtQuick
import "../qml" as UI

Item {
    id: harness
    width: 500; height: 500

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    property var sampleItem:  ({ title: "Blade Runner 2049", year: "2017", rating: "8.7", cover: "" })
    property var noRatingItem: ({ title: "Untitled Doc", year: "2020", rating: "", cover: "" })

    // classic subject (default profile) — must stay byte-behaviour identical to today's Discover card.
    UI.CataloguePosterCard {
        id: card
        width: 140; height: 240
        item: harness.sampleItem
    }

    // gallery subject — the approved Theatre polish profile; Theatre supplies "IMDb" as the source.
    UI.CataloguePosterCard {
        id: galleryCard
        width: 148; height: 257
        visualProfile: "gallery"
        hoverSourceText: "IMDb"
        item: harness.sampleItem
    }

    // grid construction smoke — the shared infinite wall must build with a model.
    UI.CataloguePosterGrid {
        id: grid
        width: 600; height: 400
        items: [ harness.sampleItem, harness.noRatingItem ]
        loading: false
    }

    Timer {
        interval: 80; running: true; repeat: false
        onTriggered: {
            // ══ classic profile: unchanged rating behaviour ══
            ok(card.posterWidthToken === 132, "classic poster width token unchanged (132)");
            ok(card.posterRadiusToken === 8, "classic radius token unchanged (8)");
            ok(card.titleLineCount === 1, "classic title stays single-line");
            ok(card.ratingVisibleAtRest === false, "classic: rating hidden at rest");
            ok(card.ratingVisible === false, "classic: rating not visible before hover");
            card.testHovered = true;
            ok(card.ratingVisible === true, "classic: rating visible on hover");
            ok(card.ratingText === "★ 8.7", "classic: rating uses '★ <value>', got '" + card.ratingText + "'");
            ok(card.centerPlayVisible === true, "classic keeps its centered play ring on hover");
            card.testHovered = false;
            card.keyboardFocused = true;
            ok(card.ratingVisible === false, "classic: keyboard focus does not reveal the rating");
            card.keyboardFocused = false;
            card.item = harness.noRatingItem; card.testHovered = true;
            ok(card.ratingText === "" && card.ratingVisible === false, "classic: absent rating renders no badge");
            card.testHovered = false; card.item = harness.sampleItem;
            ok(card.capText === "Blade Runner 2049", "classic: title text preserved");
            ok(card.item === harness.sampleItem, "classic: card carries the ORIGINAL item object");

            // ── skeleton: no item, nothing revealed, activation disabled ──
            card.item = null; card.skeleton = true; card.testHovered = true;
            ok(card.item === null, "skeleton card has no item (activation would carry nothing)");
            ok(card.ratingVisible === false, "skeleton never reveals a rating");
            ok(card.capText === "", "skeleton shows no title text");
            ok(card.centerPlayVisible === false, "skeleton shows no play ring");
            card.skeleton = false; card.testHovered = false; card.item = harness.sampleItem;

            // ══ gallery profile: approved geometry + restrained metadata ══
            ok(galleryCard.posterWidthToken === 148, "gallery width token (148)");
            ok(galleryCard.posterRadiusToken === 12, "gallery radius token (12)");
            ok(galleryCard.titleLineCount === 2 && galleryCard.titleReserve === 35, "two-line title reserve (35)");
            ok(!galleryCard.centerPlayVisible, "gallery has no centered play ring");
            ok(!galleryCard.ratingVisible, "gallery: rating hidden at rest");
            galleryCard.testHovered = true;
            ok(galleryCard.ratingVisible && galleryCard.hoverSourceText === "IMDb", "gallery: hover reveals rating + IMDb source");
            ok(galleryCard.sourceVisible === true, "gallery: source attribution shows on hover");
            galleryCard.testHovered = false; galleryCard.keyboardFocused = true;
            ok(!galleryCard.ratingVisible, "gallery: focus does not imitate hover");
            ok(galleryCard.sourceVisible === false, "gallery: focus does not reveal the source attribution");
            galleryCard.keyboardFocused = false;

            // ── gallery: a missing rating shows no empty rating, and an absent source shows no label ──
            galleryCard.item = harness.noRatingItem; galleryCard.testHovered = true;
            ok(galleryCard.ratingVisible === false, "gallery: absent rating renders nothing");
            galleryCard.hoverSourceText = "";
            ok(galleryCard.sourceVisible === false, "gallery: empty source produces no label");
            galleryCard.hoverSourceText = "IMDb"; galleryCard.testHovered = false; galleryCard.item = harness.sampleItem;

            // ── accessibility: the card exposes a button role and a name equal to its title ──
            ok(galleryCard.accessibleName === "Blade Runner 2049", "accessible name equals the title");
            ok(galleryCard.item === harness.sampleItem, "gallery: card carries the ORIGINAL item object");

            // ── grid smoke: constructs with a two-item model ──
            ok(grid.count === grid.items.length, "shared grid builds its model, count=" + grid.count);

            if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "));
            else console.log("CATALOGUE_POSTER_CARD_OK");
            Qt.exit(harness.fails.length);
        }
    }
}
