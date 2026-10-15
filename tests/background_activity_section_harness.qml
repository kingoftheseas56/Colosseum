// tests/background_activity_section_harness.qml
// Verdict rides the exit code (Qt.exit(0) pass / Qt.exit(1) fail). Failures are
// accumulated and the process exits ONCE — Qt.exit() does not halt the current
// function, so a per-check exit would be overwritten by a later one.
import QtQuick
import "../qml"

Item {
    id: root
    width: 400
    height: 300

    QtObject {
        id: fakeRegistry
        property var activities: [
            { id: "guided:onepiece", title: "Analyzing One Piece pages",
              stage: "Detecting panels", progress: 0.4, paused: false, canPause: true },
            { id: "align:dune", title: "Syncing Dune audiobook",
              stage: "Aligning words", progress: 0.75, paused: true, canPause: true }
        ]
        property var pauseCalls: []
        property var resumeCalls: []
        function requestPause(id) { pauseCalls = pauseCalls.concat([id]) }
        function requestResume(id) { resumeCalls = resumeCalls.concat([id]) }
    }

    QtObject {
        id: emptyRegistry
        property var activities: []
        function requestPause(id) {}
        function requestResume(id) {}
    }

    BackgroundActivitySection {
        id: section
        width: 360
        registry: fakeRegistry
    }

    BackgroundActivitySection {
        id: emptySection
        width: 360
        registry: emptyRegistry
    }

    Component.onCompleted: {
        var fails = [];
        if (section.rowCount !== 2)
            fails.push("expected 2 rows, got " + section.rowCount);
        if (!section.visible)
            fails.push("section with rows must be visible");
        if (emptySection.visible)
            fails.push("empty section must vanish entirely");
        if (fails.length) {
            console.error("FAILS: " + fails.join(" | "));
            Qt.exit(1);
            return;
        }
        Qt.exit(0);
    }
}
