import QtQuick
import QtTest
import "../../qml/vinyl/VinylNotePicker.js" as Picker

TestCase {
    name: "VinylNotePicker"
    function test_verified() {
        compare(Picker.pick([
            {
                text: "Short",
                votes: 30
            },
            {
                text: "An original verified note",
                votes: 1,
                verified: true
            }
        ], 30000).text, "An original verified note");
    }
    function test_shortest() {
        compare(Picker.pick([
            {
                text: "An original longer note",
                votes: 20
            },
            {
                text: "Brief note",
                votes: 12
            }
        ], 30000).text, "Brief note");
    }
    function test_votes() {
        compare(Picker.pick([
            {
                text: "Tiny",
                votes: 1
            },
            {
                text: "A meaningful note",
                votes: 20
            }
        ], 30000).text, "A meaningful note");
    }
    function test_noTime() {
        compare(Picker.pick([
            {
                text: "This note cannot fit",
                votes: 20
            }
        ], 10), null);
    }
    function test_empty() {
        compare(Picker.pick([], 30000), null);
    }
}
