// Headless load + data harness for OnePieceUniversePage — catches a Loader-loaded page's
// QML errors and asserts the curation: 11 sagas, 2 adaptations, 2 film eras / 17 films,
// 8 manga, the one anime pin, and id→poster dressing. Verdict = exit code.
import QtQuick

Item {
    width: 1280; height: 720
    Component.onCompleted: {
        var c = Qt.createComponent("../qml/OnePieceUniversePage.qml")
        if (c.status === Component.Error) { console.log("LOAD ERROR: " + c.errorString()); Qt.exit(2); return }
        var p = c.createObject(this, { universeName: "One Piece", width: 1280, height: 720 })
        if (!p) { console.log("CREATE FAILED"); Qt.exit(3); return }
        var u = p.uni
        var roomsOk = p.roomLabels.length === 4
                      && p.roomLabels[0] === "WATCH"
                      && p.roomLabels[1] === "READ"
                      && p.roomLabels[2] === "FILMS"
                      && p.roomLabels[3] === "ADAPTATIONS"
                      && p.roomCount("WATCH") === 1
                      && p.roomCount("READ") === 8
                      && p.roomCount("FILMS") === 17
                      && p.roomCount("ADAPTATIONS") === 2
        var films = 0
        for (var i = 0; i < u.filmEras.length; i++) films += u.filmEras[i].films.length
        console.log("sagas=" + u.sagas.length + " adaptations=" + u.adaptations.length
                    + " filmEras=" + u.filmEras.length + " films=" + films + " manga=" + u.manga.length)
        console.log("anime=" + (u.anime ? u.anime.id : "MISSING")
                    + " poster=" + (u.anime ? p.poster(u.anime.id) : "-"))
        var hasWatch = (typeof p.watchRequested !== "undefined")
        var hasSeries = (typeof p.seriesRequested !== "undefined")
        // the saga/anime click must carry type:"series" so Theatre opens the series view
        var ws = p.watchSeries(u.anime)
        var wm = p.watchMovie(u.filmEras[0].films[0])
        var watchOk = !!ws && ws.id === "tt0388629" && ws.type === "series" && ws.title === "One Piece"
                      && !!wm && wm.type === "movie" && String(wm.id).indexOf("tt") === 0
        console.log("watchSeries(anime)=" + JSON.stringify(ws))
        var ok = u.sagas.length === 11 && u.adaptations.length === 2 && u.filmEras.length === 2
                 && films === 17 && u.manga.length === 8 && !!u.anime && u.anime.id === "tt0388629"
                 && hasWatch && hasSeries && watchOk && roomsOk
                 && p.poster(u.anime.id).indexOf("live.metahub.space") !== -1
        console.log(ok ? "PASS" : "FAIL (watch=" + hasWatch + " series=" + hasSeries
                    + " watchOk=" + watchOk + " roomsOk=" + roomsOk + ")")
        Qt.exit(ok ? 0 : 1)
    }
}
