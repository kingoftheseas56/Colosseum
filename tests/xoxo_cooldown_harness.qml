// Headless behavioral harness for XoxoApi's cooldown state machine (Spec A).
// Verdict rides the EXIT CODE (Qt.exit(0) pass / non-zero fail; try/catch → Qt.exit).
// The clock is injected (qml.exe forbids Date.now in scripts, and it keeps the machine
// pure + testable). Run via tests/test_xoxo_cooldown_p0.ps1.
import QtQuick
import "../qml/XoxoApi.js" as Xoxo

QtObject {
    function ok(c, m) { if (!c) { console.log("FAIL: " + m); Qt.exit(1) } }
    Component.onCompleted: {
        try {
            Xoxo._resetCooldown()
            Xoxo._noteBlock(1000)                         // first strike at t=1000ms
            ok(Xoxo._cool().blocked, "blocked after a strike")
            ok(Xoxo._cool().retryAtMs === 1000 + 90000, "first backoff = 90s, got " + (Xoxo._cool().retryAtMs - 1000))
            Xoxo._noteBlock(1000)                         // second strike
            ok(Xoxo._cool().retryAtMs === 1000 + 180000, "second backoff doubles to 3m, got " + (Xoxo._cool().retryAtMs - 1000))
            Xoxo._noteBlock(1000)                         // third strike
            ok(Xoxo._cool().retryAtMs === 1000 + 360000, "third backoff = 6m, got " + (Xoxo._cool().retryAtMs - 1000))
            ok(Xoxo._shouldFire(1000 + 100000) === false, "queue refuses while blocked (before retryAt)")
            ok(Xoxo._shouldFire(1000 + 400000) === true, "queue fires once past retryAt")
            Xoxo._noteSuccess()
            ok(!Xoxo._cool().blocked, "success clears the block")
            ok(Xoxo._cool().strikes === 0, "success resets strikes")
            ok(Xoxo._shouldFire(0) === true, "queue fires when clear")
            // backoff cap: 10 strikes must not exceed the 10-minute ceiling
            Xoxo._resetCooldown()
            for (var i = 0; i < 10; i++) Xoxo._noteBlock(0)
            ok(Xoxo._cool().retryAtMs <= 600000, "backoff caps at 10min, got " + Xoxo._cool().retryAtMs)
            console.log("XOXO COOLDOWN PASS"); Qt.exit(0)
        } catch (e) { console.log("THROW: " + e); Qt.exit(1) }
    }
}
