// FootnoteCard.qml â€” the footnote/endnote peek card (TASK 9 R2).
//
// The glue detects a footnote/noteref link tap in the book iframe, extracts the note's text
// (paper_glue.js FootnoteHandler path), and emits 'footnote' { html, rect }; ReaderShell
// routes it here. We do NOT navigate the page to the note â€” the reader stays put and the note
// is shown in this glass card near the tap. v1 renders plain text (the glue strips tags).
//
// Serif body: the mock uses Literata, which is NOT bundled (only Fraunces + Inter are loaded
// by Main.qml / the harness), so we render in Theme.display (Fraunces â€” a real, loaded serif)
// rather than silently falling back to Tahoma. Bridge-free: data in via properties, dismiss
// out via signal; own click-swallow + a backdrop dismiss (house doctrine). Esc via ReaderShell.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

Item {
    id: fnCard
    objectName: "reader2FootnoteCard"

    // ---- inputs ----
    property var anchorRect: ({ x: 0, y: 0, w: 0, h: 0 })  // the tapped anchor rect (overlay coords)
    property string text: ""
    property bool shown: false

    // ---- signals up ----
    signal dismissed()

    visible: shown
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: "Footnote"
    onShownChanged: if (shown) Qt.callLater(function() { fnCard.forceActiveFocus(Qt.OtherFocusReason) })
    Keys.onPressed: function(event) {
        if (!shown) return
        if (event.key === Qt.Key_Escape) { fnCard.dismissed(); event.accepted = true; return }
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            // A footnote has one keyboard region. Tab/Shift+Tab stay on it until Esc/dismiss.
            event.accepted = true; return
        }
        popupScroll.handle(event)
    }

    readonly property int cardW: 360
    readonly property int pad: 16
    readonly property int maxBodyH: 240
    readonly property int bodyH: Math.min(bodyText.implicitHeight, maxBodyH)
    readonly property int cardH: pad * 2 + Math.max(20, bodyH)
    readonly property var pos: L.selectionMenuPos(fnCard.anchorRect, fnCard.width, fnCard.height,
                                                  fnCard.cardW, fnCard.cardH, 12, 12)

    // backdrop: tap-outside dismiss (armed only while shown)
    ReaderKeyboardArea {
        anchors.fill: parent
        enabled: fnCard.shown
        acceptedButtons: Qt.LeftButton
        onClicked: fnCard.dismissed()
    }

    Rectangle {
        id: card
        x: fnCard.pos.x
        y: fnCard.pos.y
        width: fnCard.cardW
        height: fnCard.cardH
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

        Flickable {
            id: bodyFlick
            anchors.fill: parent
            anchors.margins: fnCard.pad
            clip: true
            contentWidth: width
            contentHeight: bodyText.implicitHeight
            interactive: contentHeight > height

            Text {
                id: bodyText
                width: bodyFlick.width
                text: fnCard.text
                font.family: Theme.display   // Fraunces (loaded serif); Literata not bundled
                font.pixelSize: 14
                lineHeight: 1.4
                color: Theme.inkTitle
                wrapMode: Text.WordWrap
            }
        }
    }
    ReaderKeyboardScrollController {
        id: popupScroll
        flick: bodyFlick
        arrowScrolling: true
        homeEndEnabled: true
    }

}

