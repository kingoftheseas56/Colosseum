import QtQuick
import "../../qml" as UI
import "../../qml/Catalog.js" as Catalog

Window {
    id: win
    width: 1280
    height: 720
    visible: true
    title: "Tankoban Comics capture"
    color: "#09090b"

    property var coverNames: ["invincible.jpg", "sandman.jpg", "saga.jpg",
                              "watchmen.jpg", "sincity.jpg", "hellboy.jpg"]
    function localCover(i) {
        return String(Qt.resolvedUrl("assets/comic-covers/" + win.coverNames[i % win.coverNames.length]))
    }
    property var rows: Catalog.topComics.map(function(r, i) {
        return { caption: r.caption, cover: win.localCover(i), c1: r.c1, c2: r.c2 }
    })
    function shelfRows(start, count) {
        var out = []
        for (var i = start; i < Math.min(rows.length, start + count); ++i) {
            var r = rows[i]
            out.push({ caption: r.caption, title: r.caption, cover: r.cover,
                       gcdId: 1000 + i, c1: r.c1, c2: r.c2 })
        }
        return out
    }

    Item {
        id: posterGate
        objectName: "comicsPosterGate"
        x: -2000; y: -2000; width: 1; height: 1
        readonly property bool ready: p0.ready && p1.ready && p2.ready
                                      && p3.ready && p4.ready && p5.ready
        UI.RoundedPosterImage { id: p0; width: 148; height: 222; sources: [win.rows[0].cover] }
        UI.RoundedPosterImage { id: p1; width: 148; height: 222; sources: [win.rows[1].cover] }
        UI.RoundedPosterImage { id: p2; width: 148; height: 222; sources: [win.rows[2].cover] }
        UI.RoundedPosterImage { id: p3; width: 148; height: 222; sources: [win.rows[3].cover] }
        UI.RoundedPosterImage { id: p4; width: 148; height: 222; sources: [win.rows[4].cover] }
        UI.RoundedPosterImage { id: p5; width: 148; height: 222; sources: [win.rows[5].cover] }
    }

    UI.TopBar {
        objectName: "comicsCaptureTopBar"
        x: 46; y: 20
        width: win.width - 92
        backdrop: null
        activeMedium: "Tankoban"
    }

    Text {
        x: 46; y: 92
        text: "Comics"
        color: "#f1eee7"
        font.pixelSize: 24
        font.weight: Font.DemiBold
    }

    UI.TankobanComicsTab {
        id: tab
        objectName: "comicsCaptureTab"
        x: 46; y: 130
        width: win.width - 92
        comicRows: win.rows
        comicShelves: [
            { label: "Most Stocked", kind: "stocked", arg: "", rows: win.shelfRows(0, 6) },
            { label: "Image", kind: "publisher", arg: "Image", rows: win.shelfRows(3, 6) }
        ]
        comicBoxes: [
            { name: "Superhero", count: 420, c1: "#4c355f", c2: "#17101f" },
            { name: "Crime", count: 188, c1: "#5c3b2e", c2: "#1b100c" },
            { name: "Science Fiction", count: 214, c1: "#345466", c2: "#0e171d" },
            { name: "Fantasy", count: 197, c1: "#4c4a2e", c2: "#17160c" }
        ]
        comicCovers: win.rows.map(function(r) { return r.cover })
    }
}
