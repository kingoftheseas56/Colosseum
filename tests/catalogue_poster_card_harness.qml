// Visual-contract proof for the shared CataloguePosterCard (Theatre Deep Catalogue, Task 6).
// The product-critical rule: the IMDb-derived rating is HIDDEN at rest and revealed ONLY on
// pointer hover, using Discover's `★ <value>` treatment — keyboard focus must not reveal it.
// NEVER throw offscreen: collect fails, print the OK marker only when clean, single Qt.exit.
import QtQuick
import "../qml" as UI

Item {
    id: harness
    width: 400; height: 400

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    property var sampleItem:  ({ title: "Blade Runner 2049", year: "2017", rating: "8.7", cover: "" })
    property var noRatingItem: ({ title: "Untitled Doc", year: "2020", rating: "", cover: "" })

    UI.CataloguePosterCard {
        id: card
        width: 140; height: 240
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
            // ── rating hidden at rest ──
            ok(card.ratingVisibleAtRest === false, "rating hidden at rest");
            ok(card.ratingVisible === false, "rating not visible before hover");

            // ── pointer hover reveals the rating with the ★ treatment ──
            card.testHovered = true;
            ok(card.ratingVisible === true, "rating visible on hover");
            ok(card.ratingText === "★ 8.7", "rating uses '★ <value>', got '" + card.ratingText + "'");
            ok(card.ratingVisibleAtRest === false, "ratingVisibleAtRest is invariantly false even while hovered");

            // ── keyboard focus must NOT invent the hover reveal ──
            card.testHovered = false;
            card.keyboardFocused = true;
            ok(card.ratingVisible === false, "keyboard focus does not reveal the rating");
            card.keyboardFocused = false;

            // ── an absent rating renders no empty badge, even on hover ──
            card.item = harness.noRatingItem;
            card.testHovered = true;
            ok(card.ratingText === "" && card.ratingVisible === false, "absent rating renders no badge");
            card.testHovered = false;

            // ── title stays visible; the card carries the original item object (what activation passes) ──
            card.item = harness.sampleItem;
            ok(card.capText === "Blade Runner 2049", "title text preserved");
            ok(card.item === harness.sampleItem, "card carries the ORIGINAL item object (activation identity)");

            // ── skeleton: no item, nothing revealed, MouseArea disabled (enabled: !skeleton) ──
            card.item = null;
            card.skeleton = true;
            card.testHovered = true;
            ok(card.item === null, "skeleton card has no item (activation would carry nothing)");
            ok(card.ratingVisible === false, "skeleton never reveals a rating");
            ok(card.capText === "", "skeleton shows no title text");
            card.skeleton = false; card.testHovered = false;

            // ── grid smoke: constructs with a two-item model ──
            ok(grid.count === grid.items.length, "shared grid builds its model, count=" + grid.count);

            if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "));
            else console.log("CATALOGUE_POSTER_CARD_OK");
            Qt.exit(harness.fails.length);
        }
    }
}
