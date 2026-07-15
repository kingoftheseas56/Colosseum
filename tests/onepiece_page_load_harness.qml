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
        var films = 0
        for (var i = 0; i < u.filmEras.length; i++) films += u.filmEras[i].films.length
        console.log("sagas=" + u.sagas.length + " adaptations=" + u.adaptations.length
                    + " filmEras=" + u.filmEras.length + " films=" + films + " manga=" + u.manga.length)
        console.log("anime=" + (u.anime ? u.anime.id : "MISSING")
                    + " poster=" + (u.anime ? p.poster(u.anime.id) : "-"))
        var hasWatch = (typeof p.watchRequested !== "undefined")
        var hasSeries = (typeof p.seriesRequested !== "undefined")
        var ok = u.sagas.length === 11 && u.adaptations.length === 2 && u.filmEras.length === 2
                 && films === 17 && u.manga.length === 8 && !!u.anime && u.anime.id === "tt0388629"
                 && hasWatch && hasSeries
                 && p.poster(u.anime.id).indexOf("live.metahub.space") !== -1
        console.log(ok ? "PASS" : "FAIL (watch=" + hasWatch + " series=" + hasSeries + ")")
        Qt.exit(ok ? 0 : 1)
    }
}
