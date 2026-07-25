import QtQuick
import "../qml" as UI

Item {
    UI.DiscoverPicker {
        id: p
        label: "Genre"
        options: [ { key: "a", text: "Action", sub: "" }, { key: "b", text: "Drama", sub: "Cinemeta" } ]
        currentKey: "b"
    }
    Timer {
        interval: 300; running: true; repeat: false
        onTriggered: {
            var fails = [];
            if (!p.current || p.current.text !== "Drama") fails.push("current lookup broken");
            if (fails.length) console.log("FAILS: " + fails.join("; "));
            else console.log("discover_picker_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
