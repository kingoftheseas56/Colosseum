// Lazy-Loader gate for ShelvesUniversePage (the anime universes' shelves centre) — boot
// smokes are blind to creation-time errors behind an inactive Loader, so this
// instantiates the page for real. (backdrop stays null under the harness — Glass is
// null-safe; creation succeeding is exactly what READY proves.) Also pins the loader's
// contract against the pinned canon: Dragon Ball's first push is 8 manga (the series =
// the two the story pairing names), 7 anime, and its films still grouped in three eras.
import QtQuick
import QtQuick.Window
import "../qml/SagaApi.js" as Saga

Window {
    visible: true
    width: 300; height: 200
    Component.onCompleted: {
        Saga.loadShelves("Dragon Ball", function(u) {
            console.log("[diag] manga=" + u.manga.length + " shows=" + u.shows.length
                        + " eras=" + u.filmEras.map(function(g) { return g.era + ":" + g.films.length }).join("|"))
            const main = u.manga.filter(function(m) { return m.main === true })
            const ok = u.manga.length === 8 && main.length === 2 && main[0].title === "Dragon Ball"
                       && main[1].title === "Dragon Ball Super"
                       && u.shows.length === 7
                       && u.filmEras.length === 3
                       && u.filmEras[0].era === "The Dragon Ball Films" && u.filmEras[0].films.length === 4
                       && u.filmEras[1].films.length === 17 && u.filmEras[2].films.length === 4
            if (!ok) {
                console.log("[diag] HARNESS FAIL — Dragon Ball shelves payload wrong")
                Qt.exit(2)
            }
        })
        Saga.loadShelves("One Piece", function(u) {
            const live = u.shows.filter(function(s) { return s.live === true })
            const up = u.shows.filter(function(s) { return s.upcoming === true })
            const ok = u.manga.length === 8 && u.shows.length === 3
                       && live.length === 1 && live[0].title === "One Piece — Live Action"
                       && up.length === 1 && up[0].title === "The One Piece"
                       && u.filmEras.length === 2
            if (!ok) {
                console.log("[diag] HARNESS FAIL — One Piece shelves payload wrong")
                Qt.exit(2)
            }
        })
    }
    Loader {
        id: l
        source: "../qml/ShelvesUniversePage.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR — see warnings above")
            if (status === Loader.Ready) console.log("[diag] LOADER READY — page created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}
