// LeftPanel.qml — the reader's LEFT GLASS PANEL (TASK 8): a slide-in tabbed column
// with Contents / Bookmarks / Highlights (+ a DISABLED Audio tab whose real content is
// Task 13). 348px glass over the paper, sliding in from the left edge with the mock's
// ~.32s cubic. Pixel contract: the chrome mock's `.panel.left`, `.tabs`, `.pane`,
// `.chrow`, `.mark`, `.hl` (agents/colosseum-book-reader-chrome-mock.html).
//
// Like TopBar/BottomRail this overlay is BRIDGE-FREE: it takes its data via properties
// and reports back via signals only, so ReaderShell keeps sole ownership of the paper +
// the native stores (bookmarks.json / annotations.json through Reader2Bridge). Row
// SHAPING is pure (Reader2Logic.tocRowState/bookmarkRow/highlightRow) so it renders BOTH
// reader2's write shape AND the old reader's records with zero migration.
//
// Dismissal: a transparent click-catcher over the paper to the RIGHT of the column
// (below the top bar, so the TopBar's right icons stay live) emits closeRequested — the
// familiar "tap the page to close the drawer". The Contents icon toggles it too, and Esc
// closes it (both wired in ReaderChrome / ReaderShell).
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

Item {
    id: panel

    // ---- inputs (bound by ReaderChrome from ReaderShell's view-model) ----
    property bool open: false
    property string activeTab: "contents"       // contents | bookmarks | highlights | audio
    property var tocModel: []                    // [{ index, label, href, fraction? }]
    property int currentTocIndex: -1             // from relocated.tocIndex
    property var bookmarks: []                   // raw records from bookmarksGet
    property var highlights: []                  // raw records from annotationsGet

    // ---- signals up ----
    signal closeRequested()
    signal tabSelected(string tab)
    signal tocActivated(string href)
    signal bookmarkActivated(string cfi)
    signal bookmarkDeleted(string id)
    signal highlightActivated(string cfi)

    readonly property int colWidth: 348
    readonly property int topBarPx: 64           // keep the top bar's right icons clickable

    // ---------- click-outside-to-dismiss (transparent; paper area right of the column) ----------
    // Only armed while open, and inset below the top bar so the TopBar icons stay live.
    MouseArea {
        anchors.left: column.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: panel.topBarPx
        anchors.bottom: parent.bottom
        enabled: panel.open
        onClicked: panel.closeRequested()
    }

    // ---------- the glass column ----------
    Rectangle {
        id: column
        width: panel.colWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        color: Theme.panelBg

        // slide in/out from the left edge (mock: transform .32s cubic-bezier(.2,.8,.2,1)).
        transform: Translate {
            x: panel.open ? 0 : -column.width
            Behavior on x {
                NumberAnimation {
                    duration: 320
                    easing.type: Easing.Bezier
                    easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1]
                }
            }
        }

        // right hairline border (mock border-right: 1px var(--bar-border)).
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.barBorder
        }

        // OWN click-swallow (house doctrine): taps/scrolls inside the column never fall
        // through to the paper or the chrome's double-click toggle beneath.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: (w) => { w.accepted = true }
        }

        // ---------- tab strip ----------
        Row {
            id: tabStrip
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 18
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 2

            readonly property real tabW: (width - spacing * 3) / 4

            Tab { width: tabStrip.tabW; label: "Contents";   active: panel.activeTab === "contents";   onPicked: panel.tabSelected("contents") }
            Tab { width: tabStrip.tabW; label: "Bookmarks";  active: panel.activeTab === "bookmarks";  onPicked: panel.tabSelected("bookmarks") }
            Tab { width: tabStrip.tabW; label: "Highlights"; active: panel.activeTab === "highlights"; onPicked: panel.tabSelected("highlights") }
            // Audio is DISABLED until Task 13 — rendered so the strip's 4-tab shape is
            // fixed now (dim, non-interactive, tooltip on hover).
            Tab { width: tabStrip.tabW; label: "Audio"; active: panel.activeTab === "audio"; tabEnabled: false; tip: "Audio — soon" }
        }

        // ---------- pane body ----------
        Item {
            id: body
            anchors.top: tabStrip.bottom
            anchors.topMargin: 9
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            // ===== Contents =====
            Item {
                anchors.fill: parent
                visible: panel.activeTab === "contents"

                Text {
                    anchors.centerIn: parent
                    visible: !panel.tocModel || panel.tocModel.length === 0
                    text: "No chapters"
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 13
                }

                ListView {
                    id: tocList
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 6
                    anchors.topMargin: 14
                    anchors.bottomMargin: 20
                    visible: panel.tocModel && panel.tocModel.length > 0
                    clip: true
                    model: panel.tocModel
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        id: chrow
                        required property var modelData
                        required property int index
                        readonly property string rowState: L.tocRowState(chrow.index, panel.currentTocIndex)
                        width: tocList.width - 12
                        height: chLabel.implicitHeight + 20

                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: chrow.rowState === "current" ? Theme.goldWash
                                 : (chMa.containsMouse ? Theme.rowHover : "transparent")
                        }
                        Row {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12
                            Text {
                                width: 20
                                text: String(chrow.index + 1)
                                horizontalAlignment: Text.AlignLeft
                                color: chrow.rowState === "current" ? Theme.gold : Theme.inkGhost
                                font.family: Theme.ui
                                font.pixelSize: 11
                            }
                            Text {
                                id: chLabel
                                width: parent.width - 20 - parent.spacing
                                text: (chrow.modelData && chrow.modelData.label) ? String(chrow.modelData.label) : ""
                                wrapMode: Text.WordWrap
                                font.family: Theme.ui
                                font.pixelSize: 14
                                color: chrow.rowState === "current" ? Theme.gold
                                     : (chrow.rowState === "read" ? Theme.inkGhost : Theme.inkDim)
                            }
                        }
                        MouseArea {
                            id: chMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.tocActivated((chrow.modelData && chrow.modelData.href) ? String(chrow.modelData.href) : "")
                        }
                    }
                }
            }

            // ===== Bookmarks =====
            Item {
                anchors.fill: parent
                visible: panel.activeTab === "bookmarks"

                Text {
                    anchors.centerIn: parent
                    visible: !panel.bookmarks || panel.bookmarks.length === 0
                    text: "No bookmarks yet"
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 13
                }

                ListView {
                    id: bmList
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 6
                    anchors.topMargin: 14
                    anchors.bottomMargin: 20
                    visible: panel.bookmarks && panel.bookmarks.length > 0
                    clip: true
                    model: panel.bookmarks
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 2

                    delegate: Item {
                        id: bmRow
                        required property var modelData
                        readonly property var r: L.bookmarkRow(bmRow.modelData)
                        width: bmList.width - 12
                        height: bmCol.implicitHeight + 24

                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: bmMa.containsMouse ? Theme.rowHover : "transparent"
                        }
                        Column {
                            id: bmCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 6
                            Text {
                                width: parent.width
                                text: bmRow.r.where
                                visible: bmRow.r.where !== ""
                                elide: Text.ElideRight
                                font.family: Theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.6
                                font.capitalization: Font.AllUppercase
                                color: Theme.inkGhost
                            }
                            Text {
                                width: parent.width
                                text: bmRow.r.snippet
                                visible: bmRow.r.snippet !== ""
                                wrapMode: Text.WordWrap
                                font.family: Theme.display
                                font.pixelSize: 14
                                color: Theme.inkDim
                            }
                        }
                        // main click → jump to the bookmark (declared FIRST so the × sits above it)
                        MouseArea {
                            id: bmMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.bookmarkActivated(bmRow.r.cfi)
                        }
                        // hover delete
                        Text {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.rightMargin: 10
                            anchors.topMargin: 10
                            visible: bmMa.containsMouse || bmDelMa.containsMouse
                            text: "×"
                            font.family: Theme.ui
                            font.pixelSize: 15
                            color: bmDelMa.containsMouse ? Theme.ink : Theme.inkFaint
                            MouseArea {
                                id: bmDelMa
                                anchors.fill: parent
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.bookmarkDeleted(bmRow.r.id)
                            }
                        }
                    }
                }
            }

            // ===== Highlights =====
            Item {
                anchors.fill: parent
                visible: panel.activeTab === "highlights"

                Text {
                    anchors.centerIn: parent
                    visible: !panel.highlights || panel.highlights.length === 0
                    text: "No highlights yet"
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 13
                }

                ListView {
                    id: hlList
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 6
                    anchors.topMargin: 14
                    anchors.bottomMargin: 20
                    visible: panel.highlights && panel.highlights.length > 0
                    clip: true
                    model: panel.highlights
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 2

                    delegate: Item {
                        id: hlRow
                        required property var modelData
                        readonly property var r: L.highlightRow(hlRow.modelData)
                        width: hlList.width - 12
                        height: hlCol.implicitHeight + 24

                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: hlMa.containsMouse ? Theme.rowHover : "transparent"
                        }
                        Column {
                            id: hlCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 6
                            Text {
                                width: parent.width
                                text: hlRow.r.where
                                visible: hlRow.r.where !== ""
                                elide: Text.ElideRight
                                font.family: Theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.6
                                font.capitalization: Font.AllUppercase
                                color: Theme.inkGhost
                            }
                            // colored left edge-rule + serif quote (mock .hl .snippet)
                            Item {
                                width: parent.width
                                height: hlText.implicitHeight
                                Rectangle {
                                    id: hlEdge
                                    width: 3
                                    radius: 1
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    color: hlRow.r.color !== "" ? hlRow.r.color : Theme.gold
                                }
                                Text {
                                    id: hlText
                                    anchors.left: hlEdge.right
                                    anchors.leftMargin: 10
                                    anchors.right: parent.right
                                    text: hlRow.r.text
                                    wrapMode: Text.WordWrap
                                    font.family: Theme.display
                                    font.pixelSize: 14
                                    color: Theme.inkDim
                                }
                            }
                            // optional indented note (mock .mark .note)
                            Text {
                                width: parent.width
                                visible: hlRow.r.note !== ""
                                leftPadding: 10
                                text: hlRow.r.note
                                wrapMode: Text.WordWrap
                                font.family: Theme.ui
                                font.pixelSize: 13
                                color: Theme.inkFaint
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    color: Theme.noteRule
                                }
                            }
                        }
                        MouseArea {
                            id: hlMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.highlightActivated(hlRow.r.cfi)
                        }
                    }
                }
            }
        }
    }

    // ---------- a tab: label + gold underline when active; hover tooltip when disabled ----------
    component Tab: Item {
        id: tab
        property string label: ""
        property bool active: false
        property bool tabEnabled: true
        property string tip: ""
        signal picked()
        height: 36

        Text {
            id: tabLabel
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 9
            text: tab.label
            font.family: Theme.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: tab.active ? Theme.ink : (tab.tabEnabled ? Theme.inkFaint : Theme.inkGhost)
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: Theme.gold
            visible: tab.active
        }
        MouseArea {
            id: tabMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: tab.tabEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (tab.tabEnabled) tab.picked()
        }
        // minimal hover tooltip for the disabled Audio tab (no QtQuick.Controls needed).
        Rectangle {
            visible: !tab.tabEnabled && tabMa.containsMouse && tab.tip !== ""
            anchors.top: parent.bottom
            anchors.topMargin: 2
            anchors.horizontalCenter: parent.horizontalCenter
            z: 50
            radius: 6
            color: Theme.bar
            border.color: Theme.barBorder
            border.width: 1
            width: tipText.implicitWidth + 16
            height: tipText.implicitHeight + 10
            Text {
                id: tipText
                anchors.centerIn: parent
                text: tab.tip
                color: Theme.inkDim
                font.family: Theme.ui
                font.pixelSize: 12
            }
        }
    }
}
