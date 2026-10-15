// DictCard.qml â€” the Define (dictionary) glass card (TASK 9 R2).
//
// Opened from the SelectionMenu's Define action: ReaderShell extracts the first word of the
// selection, calls Reader2Bridge.dictLookup(word) (Wiktionary REST, C++ side â€” house rule
// "no network on the paper"), and feeds the parsed result here. Word in the display serif
// (Fraunces), definitions in dim UI (Inter); a quiet empty state with an "Open in Wiktionary"
// affordance. Bridge-free like the rest of the chrome â€” data in via properties, actions out
// via signals â€” so it instantiates headless (chrome smoke).
//
// Positioned near the selection by the same pure clamp the SelectionMenu uses
// (Reader2Logic.selectionMenuPos); own click-swallow MouseArea (house doctrine); a backdrop
// below it dismisses on tap-outside. Esc is routed by ReaderShell.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

Item {
    id: dictCard
    objectName: "reader2DictCard"

    // ---- inputs ----
    property var anchorRect: ({ x: 0, y: 0, w: 0, h: 0 })  // the selection rect (overlay coords)
    property string word: ""
    property var entries: []          // [{ partOfSpeech, definitions:[str,...] }] from L.dictParse
    property string dictState: "loading"  // "loading" | "ok" | "empty"
    property bool shown: false

    // ---- signals up ----
    signal dismissed()
    signal openExternal()             // "Open in Wiktionary" â†’ ReaderShell Qt.openUrlExternally

    visible: shown
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: "Dictionary definition"
    KeyNavigation.tab: openLink.visible ? openMa : dictCard
    KeyNavigation.backtab: openLink.visible ? openMa : dictCard
    KeyNavigation.priority: KeyNavigation.BeforeItem
    onShownChanged: if (shown) Qt.callLater(function() { dictCard.forceActiveFocus(Qt.OtherFocusReason) })
    Keys.onPressed: function(event) {
        if (!shown) return
        if (event.key === Qt.Key_Escape) { dictCard.dismissed(); event.accepted = true; return }
        popupScroll.handle(event)
    }

    readonly property int cardW: 320
    readonly property int pad: 16
    readonly property int maxDefsH: 220
    readonly property int bodyH: dictState === "ok"
            ? Math.min(defsColumn.implicitHeight, maxDefsH)
            : stateText.implicitHeight + (dictState === "empty" ? openLink.height + 10 : 0)
    readonly property int cardH: pad * 2 + header.implicitHeight + 12 + Math.max(20, bodyH)
    readonly property var pos: L.selectionMenuPos(dictCard.anchorRect, dictCard.width, dictCard.height,
                                                  dictCard.cardW, dictCard.cardH, 12, 12)

    // backdrop: tap-outside dismiss (armed only while shown)
    ReaderKeyboardArea {
        anchors.fill: parent
        enabled: dictCard.shown
        acceptedButtons: Qt.LeftButton
        onClicked: dictCard.dismissed()
    }

    Rectangle {
        id: card
        x: dictCard.pos.x
        y: dictCard.pos.y
        width: dictCard.cardW
        height: dictCard.cardH
        radius: 14
        color: Theme.panelBg
        border.color: Theme.barBorder
        border.width: 1
        antialiasing: true

        // OWN click-swallow (house doctrine)
        ReaderKeyboardArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: (w) => { w.accepted = true }
        }

        // word â€” display serif
        Text {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: dictCard.pad
            text: dictCard.word
            font.family: Theme.display
            font.pixelSize: 22
            font.weight: Font.Medium
            color: Theme.ink
            elide: Text.ElideRight
        }

        // ---- ok: scrollable definitions ----
        Flickable {
            id: defsFlick
            visible: dictCard.dictState === "ok"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.topMargin: 12
            anchors.bottom: parent.bottom
            anchors.leftMargin: dictCard.pad
            anchors.rightMargin: dictCard.pad
            anchors.bottomMargin: dictCard.pad
            clip: true
            contentWidth: width
            contentHeight: defsColumn.implicitHeight
            interactive: contentHeight > height

            Column {
                id: defsColumn
                width: defsFlick.width
                spacing: 10

                Repeater {
                    model: dictCard.entries
                    delegate: Column {
                        id: entry
                        required property var modelData
                        width: defsColumn.width
                        spacing: 3

                        Text {
                            text: entry.modelData.partOfSpeech || ""
                            visible: text.length > 0
                            font.family: Theme.ui
                            font.pixelSize: 11
                            font.italic: true
                            color: Theme.gold
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: entry.modelData.definitions
                            delegate: Row {
                                required property var modelData
                                required property int index
                                width: entry.width
                                spacing: 6
                                Text {
                                    text: (index + 1) + "."
                                    font.family: Theme.ui
                                    font.pixelSize: 13
                                    color: Theme.inkGhost
                                }
                                Text {
                                    width: parent.width - 18
                                    text: modelData
                                    font.family: Theme.ui
                                    font.pixelSize: 13
                                    lineHeight: 1.35
                                    color: Theme.inkDim
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- loading / empty: a single line (+ external link when empty) ----
        Text {
            id: stateText
            visible: dictCard.dictState !== "ok"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.topMargin: 12
            anchors.leftMargin: dictCard.pad
            anchors.rightMargin: dictCard.pad
            text: dictCard.dictState === "loading" ? "Looking upâ€¦" : "No definition found."
            font.family: Theme.ui
            font.pixelSize: 13
            color: Theme.inkFaint
            wrapMode: Text.WordWrap
        }

        Item {
            id: openLink
            visible: dictCard.dictState === "empty"
            anchors.left: parent.left
            anchors.top: stateText.bottom
            anchors.topMargin: 10
            anchors.leftMargin: dictCard.pad
            width: openText.implicitWidth + 4
            height: 22
            Text {
                id: openText
                anchors.verticalCenter: parent.verticalCenter
                text: "Open in Wiktionary"
                font.family: Theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: openMa.containsMouse ? Theme.gold : Theme.inkDim
            }
            ReaderKeyboardArea {
                id: openMa
                objectName: "reader2DictExternal"
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                keyboardLabel: "Open in Wiktionary"
                KeyNavigation.tab: dictCard
                KeyNavigation.backtab: dictCard
                onClicked: dictCard.openExternal()
            }
        }
    }
    ReaderKeyboardScrollController {
        id: popupScroll
        flick: defsFlick
        arrowScrolling: true
        homeEndEnabled: true
    }

}

