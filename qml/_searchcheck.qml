// PROTOTYPE harness: native\build-msvc\colosseum.exe qml\_searchcheck.qml
// Times Theatre search through the REAL launcher (so the CachingNam IPv4 pins apply) and
// prints the hero pick for two canonical queries — "dune" should crown the film, "game of
// thrones" the series. First done() = when the user sees the grid; second = hero enrich.
import QtQuick
import "WorldSearch.js" as WorldSearch

Item {
    id: root
    property int doneCount: 0
    property var t0: 0

    function check(query, expectHero, next) {
        var start = Date.now()
        var calls = 0
        WorldSearch.searchFor("Theatre", query, function(items) {
            calls += 1
            var ms = Date.now() - start
            if (calls === 1) {
                console.log("SEARCH[" + query + "] grid in " + ms + "ms · " + items.length +
                            " results · hero = " + (items.length ? items[0].title + " (" + items[0].group + ")" : "none"))
                if (items.length && expectHero.length &&
                    items[0].title.toLowerCase().indexOf(expectHero) < 0)
                    console.log("SEARCH[" + query + "] WARN: hero not the expected '" + expectHero + "'")
            } else {
                console.log("SEARCH[" + query + "] hero enriched at " + ms + "ms · synopsis " +
                            (items[0].synopsis.length ? items[0].synopsis.length + " chars" : "EMPTY"))
                next()
            }
        })
    }

    Component.onCompleted: {
        check("dune", "dune", function() {
            check("game of thrones", "game of thrones", function() {
                console.log("SEARCHCHECK DONE")
                Qt.quit()
            })
        })
    }

    Timer { interval: 45000; running: true; onTriggered: { console.log("SEARCHCHECK TIMEOUT"); Qt.quit() } }
}
