// Deterministic scene for the lanista harness. Every interactive element is
// objectNamed — the bridge's whole targeting model rests on that discipline.
import QtQuick

Window {
    id: win
    objectName: "harnessWindow"
    width: 800; height: 600
    visible: true
    title: "LanistaHarness"

    property int clickCount: 0
    property var openCalls: []
    property string previousOpenPath: ""
    property string previousOpenId: ""
    property string previousOpenTitle: ""
    property string lastOpenPath: ""
    property string lastOpenId: ""
    property string lastOpenTitle: ""

    // Function 0007 bridge contract fixture. This is deliberately a tiny fake
    // root seam: the native command must invoke the root's production-shaped
    // openBookSession(path, book) method, never a generic QML mutation surface.
    function openBookSession(path, book) {
        var b = book || ({})
        var calls = win.openCalls.slice()
        if (calls.length > 0) {
            win.previousOpenPath = calls[calls.length - 1].path
            win.previousOpenId = calls[calls.length - 1].book.id || ""
            win.previousOpenTitle = calls[calls.length - 1].book.title || ""
        }
        calls.push({ "path": String(path), "book": b })
        win.openCalls = calls
        win.lastOpenPath = String(path)
        win.lastOpenId = b.id || ""
        win.lastOpenTitle = b.title || ""
        bookReaderShell.bookPath = String(path)
        bookReaderShell.bookReady = false
        bookReaderShell.pendingSaveBookPath = String(path)

        // The bridge invokes B synchronously at A's pending-save transition.
        // Emulate the production flush by writing A's Continue record only when
        // the second open arrives. A timeout metadata title intentionally leaves
        // B unready so the native timeout/cleanup contract is testable.
        if (calls.length >= 2 && b.title !== "timeout") {
            var a = calls[calls.length - 2]
            if (typeof Progress !== "undefined") {
                Progress.record({
                    "id": a.book.id,
                    "kind": "book",
                    "caption": a.book.title,
                    "title": a.book.title,
                    "progress": 0.42,
                    "resume": { "path": a.path, "book": a.book }
                })
            }
            bookReaderShell.bookReady = true
        }
    }

    Rectangle { anchors.fill: parent; color: "#101218" }

    Rectangle {
        id: counterButton
        objectName: "counterButton"
        x: 100; y: 100; width: 200; height: 48
        radius: 8; color: ma.containsMouse ? "#2a3242" : "#1a2030"
        Text {
            objectName: "counterLabel"
            anchors.centerIn: parent
            text: "clicks: " + win.clickCount
            color: "#f0f0f0"
        }
        MouseArea {
            id: ma; objectName: "counterMouse"
            anchors.fill: parent; hoverEnabled: true
            onClicked: win.clickCount++
        }
    }

    TextInput {
        id: field
        objectName: "nameField"
        x: 100; y: 200; width: 200; height: 32
        color: "#f0f0f0"; font.pixelSize: 16
    }

    // An item partly OUTSIDE the window — geometry assertions must see this.
    Rectangle {
        objectName: "clippedBox"
        x: win.width - 40; y: 300; width: 120; height: 40; color: "#803030"
    }

    // A core-QtQuick ListView (no Controls import): its leaf class is
    // "QQuickListView", which carries NO "Flickable" token — so ui-snapshot must
    // walk the SUPERCLASS chain (QQuickListView -> QQuickFlickable) to mark it
    // interactive. This is the fixture that pins the chain-walk over a leaf check.
    // A scrollable ListView: contentHeight (20*24=480) far exceeds height (120),
    // so a wheel scroll has somewhere to go — ui-scroll asserts contentY moves.
    ListView {
        objectName: "mainList"
        x: 100; y: 260; width: 200; height: 120; clip: true
        model: 20
        delegate: Rectangle {
            required property int index
            objectName: "listRow" + index
            width: 200; height: 24
            color: index % 2 ? "#181c26" : "#12151d"
            Text { text: "item " + parent.index; color: "#c0c0c0"; x: 6; y: 4 }
        }
    }

    Flickable {
        objectName: "longList"
        x: 400; y: 100; width: 300; height: 400
        contentWidth: width; contentHeight: col.height
        Column {
            id: col
            Repeater {
                model: 40
                delegate: Rectangle {
                    required property int index
                    objectName: "row" + index
                    width: 300; height: 50
                    color: index % 2 ? "#181c26" : "#12151d"
                    Text { text: "row " + parent.index; color: "#c0c0c0"; x: 8; y: 14 }
                }
            }
        }
    }

    // A focusable key sink for ui-keypress: a plain Item has no click-to-focus of
    // its own, so its MouseArea calls forceActiveFocus() — ui-click the area to
    // focus it, then ui-keypress lands on Keys.onPressed and lastKey records e.text.
    Item {
        id: keySink
        objectName: "keySink"
        x: 100; y: 400; width: 200; height: 32
        property string lastKey: ""
        Keys.onPressed: (event) => { keySink.lastKey = event.text }
        MouseArea {
            objectName: "keySinkMouse"
            anchors.fill: parent
            onClicked: keySink.forceActiveFocus()
        }
    }

    Item {
        id: bookReaderShell
        objectName: "bookReaderShell"
        visible: false
        property string bookPath: ""
        property bool bookReady: false
        // Baseline seam supported by the bridge; Slice 4 adds the replacement
        // pendingSaveContext property to production ReaderShell.
        property string pendingSaveBookPath: ""
        width: 1; height: 1
    }

    Item {
        objectName: "reader2RootProbe"
        property string previousOpenPath: win.previousOpenPath
        property string previousOpenId: win.previousOpenId
        property string previousOpenTitle: win.previousOpenTitle
        property string lastOpenPath: win.lastOpenPath
        property string lastOpenId: win.lastOpenId
        property string lastOpenTitle: win.lastOpenTitle
        visible: false
        width: 1; height: 1
    }

    // ── L1-Bridge structural fixtures (2026-08-13) ──────────────────────────
    // dump-ui/ui-query's all-item walk, parent chain, and clipping chain are
    // proven against THESE — deliberately unnamed, nested, clipped, transparent,
    // disabled, and zero-size items — never against the interactive fixtures
    // above (which stay untouched; they exist for ui-click/ui-snapshot).
    Item {
        id: structuralFixtures
        objectName: "structuralFixtures"
        x: 0; y: 0; width: 1; height: 1

        // THE unnamed-item fixture, isolated in its OWN named host so the
        // negative control for structural_dump_includes_unnamed_items can
        // remove ONLY the inner Rectangle (leaving parentChainHost's own
        // unnamed middle hop below untouched) and make exactly that one case
        // go red — nothing else roots here, so nothing else is affected.
        Item {
            id: unnamedItemHost
            objectName: "unnamedItemHost"
            x: 10; y: 500; width: 24; height: 24
            Rectangle {
                // deliberately no objectName — THE fixture the negative control removes
                x: 0; y: 0; width: 24; height: 24; color: "#404040"
            }
        }

        // A named leaf parented under an UNNAMED middle item, itself parented
        // under a NAMED host — proves parent-chain resolution survives a hop
        // with no objectName, not just the named-to-named case.
        Item {
            id: parentChainHost
            objectName: "parentChainHost"
            x: 10; y: 540; width: 40; height: 40
            Rectangle {
                // deliberately no objectName — the middle hop
                x: 4; y: 4; width: 30; height: 30; color: "#505050"
                Rectangle {
                    objectName: "parentChainLeaf"
                    x: 4; y: 4; width: 10; height: 10; color: "#606060"
                }
            }
        }

        // A single clip:true ancestor whose child sits OUTSIDE its own bounds
        // but fully inside the window — clippedByWindow must read false while
        // the clip chain names the real reason it is not actually on screen
        // (L1-Discovery row 4, the sharpest gap measured).
        Item {
            id: clipHost
            objectName: "clipHost"
            x: 500; y: 400; width: 60; height: 40
            clip: true
            Rectangle {
                objectName: "clipHostChild"
                x: 100; y: 100; width: 20; height: 20; color: "#ff0000"
            }
        }

        // Two NESTED clip:true ancestors between the leaf and the window — the
        // chain must carry BOTH, nearest first, never just the outermost.
        Item {
            id: clipOuter
            objectName: "clipOuter"
            x: 20; y: 400; width: 60; height: 60
            clip: true
            Item {
                id: clipInner
                objectName: "clipInner"
                x: 0; y: 0; width: 60; height: 60
                clip: true
                Rectangle {
                    objectName: "doubleClippedChild"
                    x: 100; y: 100; width: 10; height: 10; color: "#00ff00"
                }
            }
        }

        // Transparent / disabled / zero-size — dump-ui's "no visibility filter"
        // must still report all three, with the right flags, never omit them.
        Rectangle {
            objectName: "transparentItem"
            x: 100; y: 460; width: 20; height: 20; opacity: 0; color: "#ffffff"
        }
        Rectangle {
            objectName: "disabledItem"
            x: 130; y: 460; width: 20; height: 20; enabled: false; color: "#ffffff"
        }
        Item {
            objectName: "zeroSizeItem"
            x: 160; y: 460; width: 0; height: 0
        }
    }

    // A dedicated 600-row subtree, reached ONLY via root=overBudgetContainer —
    // large enough that ITS OWN compact-JSON dump-ui reply exceeds the L1-Bridge
    // reply byte budget on its own, proving the truncation/continuation paths
    // without inflating every OTHER test's default (unscoped) dump-ui call.
    // Declared LAST: the pre-existing whole-window dump-ui check (seq 9 above)
    // finds counterButton long before DFS ever reaches this subtree.
    Item {
        id: overBudgetContainer
        objectName: "overBudgetContainer"
        x: 0; y: 0; width: 1; height: 1
        Repeater {
            model: 600
            delegate: Item {
                required property int index
                objectName: "budgetRow" + index
                x: index; y: 0; width: 2; height: 2
            }
        }
    }
}
