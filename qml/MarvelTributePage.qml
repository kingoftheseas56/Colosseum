import QtQuick

// The Marvel Cinematic Universe Tribute: the Infinity Gauntlet (Hemanth 2026-11-09).
// The six stones open Phases I–VI. Shaped like DexterTributePage: the tribute lives in mcu/
// and is loaded asynchronously; the house BackAction steps out of a phase first, then the page.
FocusScope {
    id: root
    objectName: "marvelTributePage"
    property string extensionId: ""
    property string universeName: ""
    property bool reducedMotion: false
    readonly property var gauntlet: tribute.item
    property bool focusPending: false
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var item)
    signal comicsArchiveRequested(var entry)
    signal comicTitleRequested(var entry)

    function takeKeyboardFocus() {
        focusPending = true
        if (gauntlet) {
            gauntlet.forceActiveFocus()
            focusPending = false
        } else {
            root.forceActiveFocus()
        }
    }

    // Escape inside a phase returns to the gauntlet; on the gauntlet the shell takes it.
    function requestEscape() {
        if (!gauntlet || gauntlet.activePhase === 0) return false
        gauntlet.go(0)
        return true
    }

    Rectangle { anchors.fill: parent; color: "#0c0e11" }

    Loader {
        id: tribute
        anchors.fill: parent
        asynchronous: true
        source: "mcu/InfinityGauntlet.qml"
        onLoaded: {
            item.embedded = true
            item.watchRequested.connect(root.watchRequested)
            item.comicsArchiveRequested.connect(root.comicsArchiveRequested)
            item.comicTitleRequested.connect(root.comicTitleRequested)
            if (root.focusPending) root.takeKeyboardFocus()
        }
    }

    BackAction {
        x: 28
        y: 24
        z: 30
        variant: "capsule"
        tip: root.gauntlet && root.gauntlet.activePhase > 0 ? qsTr("The gauntlet") : ""
        onTriggered: if (!root.requestEscape()) root.backRequested()
    }

    Text {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 440)
        visible: tribute.status !== Loader.Ready
        text: tribute.status === Loader.Error ? qsTr("This build could not load the Infinity Gauntlet.")
                                              : qsTr("Opening the gauntlet…")
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: "#c9c8d0"
        font.pixelSize: 18
    }
}
