// Headless harness for the saga template's pure canon logic: SagaApi.normTitle +
// slotByCanon (search hits → canon-ordered slots, no fuzz survives). Importing SagaApi
// also proves its `.import` chain (Universes.js + BiblioApi.js) parses — a broken .import
// only fails at RUNTIME (the leading-dot lesson). Verdict rides the exit code.
import QtQuick
import "../qml/SagaApi.js" as Saga
import "../qml/Universes.js" as UDB

QtObject {
    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.log("HARNESS FAIL: " + e.message); Qt.exit(2) }
    }
    function runChecks() {
        // --- normTitle: punctuation / numeral variants land on the same key ---
        if (Saga.normTitle("Harry Potter and the Deathly Hallows: Part I")
            !== Saga.normTitle("Harry Potter and the Deathly Hallows - Part 1"))
            throw new Error("Part I / Part 1 must normalize together")
        if (Saga.normTitle("Batman & Mr. Freeze") !== Saga.normTitle("Batman and Mr Freeze"))
            throw new Error("ampersand must normalize to and")

        // --- slotByCanon: canon order wins, fuzz dies, missing slots stay empty ---
        var canon = [ "A Game of Thrones", "A Clash of Kings", "A Storm of Swords" ]
        var hits = [
            { name: "A Storm of Swords" },                    // arrives first, belongs third
            { name: "Game of Thrones: The Last Watch" },      // fuzz — matches NO canon slot
            { name: "A Game of Thrones" }
        ]
        var slots = Saga.slotByCanon(canon, hits)
        if (slots[0].name !== "A Game of Thrones") throw new Error("slot 0 wrong: " + JSON.stringify(slots[0]))
        if (slots[1] !== undefined) throw new Error("unmatched canon slot must stay empty (no fuzzy stand-in)")
        if (slots[2].name !== "A Storm of Swords") throw new Error("slot 2 wrong")

        // --- the saga spines are curated: every saga universe carries ordered novels ---
        var sagas = ["Harry Potter", "Lord of the Rings", "A Song of Ice and Fire", "Dune"]
        for (var i = 0; i < sagas.length; i++) {
            var c = UDB.configFor(sagas[i])
            if (c.category !== "saga") throw new Error(sagas[i] + " must ride the saga template")
            if (!c.novels || c.novels.length < 4) throw new Error(sagas[i] + " must carry its ordered novels")
        }
        if (UDB.configFor("Harry Potter").novels[0] !== "Harry Potter and the Sorcerer's Stone")
            throw new Error("HP reading order must start at book one")
        if (UDB.configFor("Harry Potter").films.length !== 11)
            throw new Error("HP film canon must be exactly 11 (Wikipedia-checked: 8 HP + 3 FB)")
        // western IPs left on the generic template must never wear the manga machinery
        if (UDB.configFor("Star Trek").readMode !== "none") throw new Error("Star Trek must suppress the manga side")
        if (UDB.configFor("DC Animated Universe").seriesLabel !== "Shows") throw new Error("DCAU row label must be Shows")
    }
}
