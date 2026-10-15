import QtQuick
import "../../qml" as UI

Window {
    id: win
    width: 1280
    height: 720
    visible: true
    title: "Theatre catalogue capture"
    objectName: "theatreCatalogCaptureWindow"
    readonly property url firstPosterUrl: Qt.resolvedUrl("assets/theatre-posters/dune.jpg")
    color: "#09090b"

    property var posterFiles: ["dune.jpg", "oppenheimer.jpg", "batman.jpg",
                               "spiderverse.jpg", "godzilla.jpg"]
    property var titles: ["Dune: Part Two", "Oppenheimer", "The Batman",
                          "Spider-Man: Across the Spider-Verse", "Godzilla Minus One"]

    function movie(i) {
        var n = i % win.titles.length
        var cover = Qt.resolvedUrl("assets/theatre-posters/" + win.posterFiles[n])
        return { id: "movie-" + i, title: win.titles[n], name: win.titles[n],
                 cover: cover, poster: cover, type: "movie", year: 2024 }
    }
    function items(offset) {
        var out = []
        for (var i = 0; i < 7; ++i) out.push(win.movie(i + offset))
        return out
    }
    function row(key, title, placement, offset, ranked) {
        return { key: key, title: title, placement: placement, ranked: ranked === true,
                 sourceKind: "house", sourceLabel: "Colosseum", items: win.items(offset),
                 seeAllPin: { pageKey: "movies", sourceKind: "house", rowKey: key } }
    }
    function fakeLoader(pageKey, options, push) {
        push({ pageKey: pageKey, generation: options.generation, loading: false, error: "",
               rows: [ win.row("top-10", "Top 10", 0, 0, true),
                       win.row("in-theaters", "In Theaters Now", 10, 1, false),
                       win.row("all-time", "All-Time Greats", 20, 2, false),
                       win.row("hidden-gems", "Hidden Gems", 30, 3, false) ] })
    }

    // Presentation gate: decode the exact local poster sources through the same renderer and
    // geometry before recording. This replaces the old fixed sleep, which could capture the
    // catalogue while asynchronous image decoding was still showing neutral placeholders.
    Item {
        id: posterGate
        objectName: "theatrePosterGate"
        x: -2000; y: -2000; width: 1; height: 1
        readonly property bool ready: p0.ready && p1.ready && p2.ready && p3.ready && p4.ready
        UI.RoundedPosterImage { id: p0; width: 148; height: 222; sources: [win.movie(0).cover] }
        UI.RoundedPosterImage { id: p1; width: 148; height: 222; sources: [win.movie(1).cover] }
        UI.RoundedPosterImage { id: p2; width: 148; height: 222; sources: [win.movie(2).cover] }
        UI.RoundedPosterImage { id: p3; width: 148; height: 222; sources: [win.movie(3).cover] }
        UI.RoundedPosterImage { id: p4; width: 148; height: 222; sources: [win.movie(4).cover] }
    }

    QtObject {
        id: prefs
        property bool showExplicit: false
        signal changed()
    }

    UI.TopBar {
        x: 46; y: 20
        width: win.width - 92
        backdrop: null
        activeMedium: "Theatre"
    }

    Text {
        x: 46; y: 92
        text: "Movies"
        color: "#f1eee7"
        font.pixelSize: 24
        font.weight: Font.DemiBold
    }
    Flickable {
        id: flick
        objectName: "theatreCatalogCaptureFlick"
        x: 46; y: 130
        width: win.width - 92
        height: win.height - 145
        clip: true
        contentWidth: width
        contentHeight: page.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        UI.TheatreCatalogPage {
            id: page
            objectName: "theatreCatalogCapturePage"
            width: flick.width
            pageKey: "movies"
            visualProfile: "gallery"
            viewportTop: flick.contentY
            viewportHeight: flick.height
            catalogLoader: win.fakeLoader
            contentPreferences: prefs
        }
    }
}
