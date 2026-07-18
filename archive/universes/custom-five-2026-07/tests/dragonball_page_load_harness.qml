// Headless load + data harness for DragonBallUniversePage — catches what the lazy-page
// gate can't (a Loader-loaded page's QML errors) and asserts the curation resolved:
// 7 anime orbs, 3 film eras / 25 films, 8 manga, and id→poster dressing. Verdict = exit code.
import QtQuick

Item {
    width: 1280; height: 720
    Component.onCompleted: {
        var c = Qt.createComponent("../qml/DragonBallUniversePage.qml")
        if (c.status === Component.Error) { console.log("LOAD ERROR: " + c.errorString()); Qt.exit(2); return }
        var p = c.createObject(this, { universeName: "Dragon Ball", anchors: undefined,
                                       width: 1280, height: 720 })
        if (!p) { console.log("CREATE FAILED"); Qt.exit(3); return }
        var u = p.uni
        var films = 0
        for (var i = 0; i < u.filmEras.length; i++) films += u.filmEras[i].films.length
        console.log("saga=" + u.saga.length + " filmEras=" + u.filmEras.length
                    + " films=" + films + " manga=" + u.manga.length)
        console.log("orb1=" + u.saga[0].era + " id=" + u.saga[0].id + " stars=" + u.saga[0].star)
        console.log("poster(orb1)=" + p.poster(u.saga[0].id))
        // signal contract: watchRequested (anime/film) + seriesRequested (manga) must exist
        var hasWatch = (typeof p.watchRequested !== "undefined")
        var hasSeries = (typeof p.seriesRequested !== "undefined")
        // the anime orbs must open as series (type:"series"), films as movies
        var ws = p.watchSeries(u.saga[0])
        var wm = p.watchMovie(u.filmEras[0].films[0])
        var watchOk = !!ws && ws.type === "series" && ws.id === "tt0088509" && ws.title === "Dragon Ball"
                      && !!wm && wm.type === "movie" && String(wm.id).indexOf("tt") === 0
        var ok = u.saga.length === 7 && u.filmEras.length === 3 && films === 25
                 && u.manga.length === 8 && hasWatch && hasSeries && watchOk
                 && p.poster(u.saga[0].id).indexOf("live.metahub.space") !== -1
        console.log(ok ? "PASS" : "FAIL (watch=" + hasWatch + " series=" + hasSeries + " watchOk=" + watchOk + ")")
        Qt.exit(ok ? 0 : 1)
    }
}
