import QtQuick
import QtTest
import "../../qml/NextUp.js" as NextUp

TestCase {
    name: "NextUpMetadata"

    function test_fetchedSeriesMetadataFillsSparseStremioProgressCard() {
        var card = NextUp.theatreCard({
            show: "tt0388629",
            entry: {
                id: "tt0388629:2:25",
                progress: 1
            }
        }, {
            id: "tt0388629:2:26",
            season: 2,
            num: 26,
            title: "The Next Episode"
        }, {
            name: "One Piece",
            poster: "https://images.example.test/one-piece.jpg"
        });

        compare(card.title, "One Piece");
        compare(card.caption, "One Piece");
        compare(card.cover, "https://images.example.test/one-piece.jpg");
    }
}
