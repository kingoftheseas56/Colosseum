// Offscreen persistence proof for ContentPreferences (Tankoban Discover, Task 2).
// NEVER throw inside an offscreen harness (it hangs qml.exe): collect fails, print the
// unique OK marker only when clean, single Qt.exit(fails.length).
//
// The test is deliberately NON-VACUOUS. A prior run could have left the temp INI holding
// showExplicit=true; if we only checked "does a reload read true?" a broken write would
// pass on that stale value. So we first write FALSE and prove a fresh instance reads it
// back (the write mechanism landed — this is the negative control), THEN write TRUE and
// prove a fresh instance reads THAT back. Only the transition surviving destroy+reload
// proves persistence. Each instance points at the SAME fixed temp INI url; instances are
// destroyed before the next is built so two QSettings never race the same file, with a
// beat between phases for the destroy-time flush to hit disk.
import QtQuick
import QtCore
import "../qml" as UI

Item {
    id: harness
    property string iniUrl: StandardPaths.writableLocation(StandardPaths.TempLocation)
                            + "/colosseum_content_prefs_test.ini"
    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label) }

    Component { id: prefsComp; UI.ContentPreferences {} }

    property var instA: null
    property var instB: null
    property var instC: null

    // Phase 0 — establish a known FALSE baseline (overwrites any stale true).
    Timer {
        interval: 20; running: true; repeat: false
        onTriggered: {
            harness.instA = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl })
            harness.ok(harness.instA !== null, "instance A must construct")
            // settingsLocation alias must point somewhere (it's what routes every instance at
            // the temp INI; if it were unset the persistence phases below could not work). Qt
            // normalises `location` to a file:// url, so assert it carries the filename rather
            // than string-equalling the raw StandardPaths input.
            harness.ok(harness.instA && String(harness.instA.settingsLocation).indexOf("colosseum_content_prefs_test.ini") !== -1,
                       "settingsLocation alias must route to the temp INI, got "
                       + (harness.instA ? harness.instA.settingsLocation : "null"))
            if (harness.instA) harness.instA.showExplicit = false
            flushA.start()
        }
    }
    Timer { id: flushA; interval: 300; repeat: false
        onTriggered: { if (harness.instA) harness.instA.destroy(); phase1.start() } }

    // Phase 1 — a fresh instance must read the FALSE baseline (write mechanism landed),
    //           then the changed() signal must fire on mutation, then we write TRUE.
    Timer { id: phase1; interval: 300; repeat: false
        onTriggered: {
            harness.instB = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl })
            harness.ok(harness.instB !== null, "instance B must construct")
            harness.ok(harness.instB && harness.instB.showExplicit === false,
                       "baseline: reload must read showExplicit=false, got "
                       + (harness.instB ? harness.instB.showExplicit : "null"))
            var fired = false
            if (harness.instB) harness.instB.changed.connect(function() { fired = true })
            if (harness.instB) harness.instB.showExplicit = true
            harness.ok(fired, "changed() must fire when showExplicit mutates")
            harness.ok(harness.instB && harness.instB.showExplicit === true,
                       "in-memory showExplicit must be true after set, got "
                       + (harness.instB ? harness.instB.showExplicit : "null"))
            flushB.start()
        }
    }
    Timer { id: flushB; interval: 300; repeat: false
        onTriggered: { if (harness.instB) harness.instB.destroy(); phase2.start() } }

    // Phase 2 — a fresh instance must read TRUE: the value survived destroy + reload.
    Timer { id: phase2; interval: 300; repeat: false
        onTriggered: {
            harness.instC = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl })
            harness.ok(harness.instC !== null, "instance C must construct")
            harness.ok(harness.instC && harness.instC.showExplicit === true,
                       "persistence: reloaded instance must read showExplicit=true, got "
                       + (harness.instC ? harness.instC.showExplicit : "null"))
            if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "))
            else console.log("CONTENT_PREFERENCES_OK")
            Qt.exit(harness.fails.length)
        }
    }
}
