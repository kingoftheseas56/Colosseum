// Headless harness for Universes.ledger() — the chip-string → ledger-row parse feeding the
// Atlas hero's media ledger. Verdict rides the exit code (try/catch → Qt.exit; a throw in
// onCompleted would HANG qml.exe).
import QtQuick
import "../qml/Universes.js" as Universes

QtObject {
    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.log("HARNESS FAIL: " + e.message); Qt.exit(2) }
    }
    function runChecks() {
        function row(r) { return r.count + "|" + r.medium }
        var l = Universes.ledger([{ t: "8 Manga" }, { t: "2 Anime" }, { t: "15 Films" }])
        if (l.length !== 3) throw new Error("expected 3 rows, got " + l.length)
        if (row(l[0]) !== "8|Manga") throw new Error("count split wrong: " + row(l[0]))
        if (row(l[2]) !== "15|Films") throw new Error("count split wrong: " + row(l[2]))

        l = Universes.ledger([{ t: "10+ Shows" }])
        if (row(l[0]) !== "10+|Shows") throw new Error("10+ must stay a count: " + row(l[0]))

        // countless mediums: single and multi-word — count is the em dash, medium verbatim
        l = Universes.ledger([{ t: "Comics" }, { t: "Graphic Novel" }])
        if (row(l[0]) !== "—|Comics") throw new Error("countless single: " + row(l[0]))
        if (row(l[1]) !== "—|Graphic Novel") throw new Error("countless multi-word: " + row(l[1]))

        if (Universes.ledger([]).length !== 0) throw new Error("empty chips must yield []")
        if (Universes.ledger(null).length !== 0) throw new Error("null chips must yield []")
    }
}
