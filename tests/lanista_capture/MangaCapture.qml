import QtQuick
import "../../qml" as UI
import "../../qml/Catalog.js" as Catalog

Window {
    id: win
    width: 1280
    height: 720
    visible: true
    title: "Tankoban Manga capture"
    color: "#09090b"

    property var coverNames: ["onepiece.jpg", "berserk.jpg", "vinland.jpg",
                              "vagabond.png", "chainsaw.png", "monster.jpg"]
    function localCover(i) {
        return String(Qt.resolvedUrl("assets/manga-covers/" + win.coverNames[i % win.coverNames.length]))
    }
    property var rows: Catalog.topManga.map(function(r, i) {
        return { caption: r.caption, title: r.caption, cover: win.localCover(i),
                 c1: r.c1, c2: r.c2 }
    })
    property var genres: Catalog.genresManga.map(function(g, i) {
        return { name: g.name, count: g.count, cover: win.localCover(i),
                 c1: g.c1, c2: g.c2 }
    })
    Item {
        id: posterGate
        objectName: "mangaPosterGate"
        x: -2000; y: -2000; width: 1; height: 1
        readonly property bool ready: p0.ready && p1.ready && p2.ready
                                      && p3.ready && p4.ready && p5.ready
        UI.RoundedPosterImage { id: p0; width: 148; height: 222; sources: [win.localCover(0)] }
        UI.RoundedPosterImage { id: p1; width: 148; height: 222; sources: [win.localCover(1)] }
        UI.RoundedPosterImage { id: p2; width: 148; height: 222; sources: [win.localCover(2)] }
        UI.RoundedPosterImage { id: p3; width: 148; height: 222; sources: [win.localCover(3)] }
        UI.RoundedPosterImage { id: p4; width: 148; height: 222; sources: [win.localCover(4)] }
        UI.RoundedPosterImage { id: p5; width: 148; height: 222; sources: [win.localCover(5)] }
    }

    UI.TopBar {
        objectName: "mangaCaptureTopBar"
        x: 46; y: 20
        width: win.width - 92
        backdrop: null
        activeMedium: "Tankoban"
    }
    Text {
        x: 46; y: 92
        text: "Manga"
        color: "#f1eee7"
        font.pixelSize: 24
        font.weight: Font.DemiBold
    }
    Flickable {
        id: flick
        objectName: "mangaCaptureFlick"
        x: 46; y: 130
        width: win.width - 92
        height: win.height - 145
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: flick.width
            spacing: 46
            UI.TrendingTop10 {
                width: parent.width
                title: "Top in Tankoban — Manga"
                items: win.rows
            }
            UI.GenreMosaic {
                width: parent.width
                title: "Explore by Genre — Manga"
                genres: win.genres
                covers: win.rows.map(function(r) { return r.cover })
            }
        }
    }
}
