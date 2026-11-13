import QtQuick
import QtTest
import "../../qml/AddonClient.js" as AddonClient

TestCase {
    name: "SmartPlayStreamability"

    function row(bytes, title, index) {
        return AddonClient.parseStream({ infoHash: String(index).repeat(40),
            title: title + "\n👤 1313 💾 12 GB",
            behaviorHints: { videoSize: bytes, filename: title + ".mkv" }
        }, "Fixture", 0, "fixture")
    }

    function test_file_size_hint_survives_normalization() {
        var small = row(679820000, "Fixture.S01E01.1080p.WEB-DL.HEVC-MeGusta", 1)
        compare(small.sizeBytes, 679820000)
        compare(small.size, "12 GB") // Displayed torrent size may describe a pack.
        verify(small.behaviorHints === undefined)
        var rich = row(2500000000, "Fixture.S01E01.1080p.WEB-DL.Atmos.5.1", 2)
        compare(SmartPlay.pick([rich, small], "1080p", {
            type: "series", title: "Fixture", season: 1, episode: 1, runtimeMinutes: 45
        }).index, 1)
    }

    function test_invalid_file_sizes_data() {
        return [ {tag: "missing", value: undefined}, {tag: "zero", value: 0},
            {tag: "negative", value: -1}, {tag: "fraction", value: 1.5},
            {tag: "string", value: "679820000"}, {tag: "nan", value: NaN},
            {tag: "infinite", value: Infinity}, {tag: "unsafe", value: 9007199254740992} ]
    }

    function test_invalid_file_sizes(data) {
        verify(row(data.value, "Fixture.S01E01.1080p.WEB-DL", 1).sizeBytes === undefined)
    }
}
