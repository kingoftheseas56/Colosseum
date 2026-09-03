// SearchSheet.qml â€” the reader's SEARCH surface (TASK 11): a thin floating glass sheet
// that drops in under the top bar. An input ("Search this book") + a result-count label
// + a scrollable list of hits; each hit is a ghost-caps chapter label over a serif excerpt
// with the matched word marked in GOLD. Pixel contract: the chrome mock's `.search`,
// `.inrow`, `.count`, `.results`, `.res`, `.rwhere`, `.rtext`, `mark`
// (agents/colosseum-book-reader-chrome-mock.html).
//
// Like the rest of the reader2 chrome this overlay is BRIDGE-FREE: it takes results via
// properties and reports up via signals only. ReaderShell owns the paper.search / goTo /
// clearSearch. SEARCH IS SUBMIT-DRIVEN (Enter), not live-on-keystroke: searching the whole
// book on every keystroke would scan the book each time â€” the cap bounds the payload, not
// the scan. So `submitted(q)` fires on Return; results flow back in via `results`.
//
// The sheet is a small centered card â€” deliberately NO full-screen backdrop, so while it is
// open the paper underneath still reads/turns (and the overlay can never block selection).
// The card carries its OWN click-swallow (house doctrine) so taps on it don't fall through
// to the paper's double-click toggle. Esc (input focused) closes it; a row click jumps and
// the sheet STAYS OPEN so you can click through hits.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

Item {
    id: sheet

    // ---- inputs (bound by ReaderChrome from ReaderShell) ----
    property bool open: false
    property var results: []            // [{ cfi, excerpt:{pre,match,post}, chapterTitle }]
    property int resultCount: 0
    property bool capped: false
    // The query whose results are currently shown ("" = no search has returned yet). Set by
    // ReaderShell when the searchResults event lands, so the empty state reads "Type to
    // search" before a search and "No results" only after one comes back empty.
    property string lastQuery: ""

    // ---- signals up ----
    signal submitted(string query)      // Return â†’ ReaderShell calls paper.search(q)
    signal resultActivated(string cfi)  // row click â†’ paper.goTo(cfi); the sheet stays open
    signal closeRequested()             // Esc â†’ ReaderChrome closes the sheet

    // fresh field + focus each time it opens (results are reset by ReaderShell on open).
    onOpenChanged: if (open) { input.text = ""; Qt.callLater(function () { input.forceActiveFocus() }) }

    readonly property int cardW: Math.min(520, Math.round(width * 0.8))
    readonly property bool hasResults: results && results.length > 0

    // ---------- the floating glass card (mock .search: top 68, centered, min(520,80%)) ----------
    Rectangle {
        id: card
        width: sheet.cardW
        x: (sheet.width - width) / 2
        y: 68
        radius: 13
        // Near-opaque: dense page text was bleeding through the old 0.88 glass and reading
        // as "shading" behind the results. Keep it a hair off solid so it still reads as glass.
        color: Qt.rgba(13 / 255, 13 / 255, 16 / 255, 0.985)
        border.color: Theme.barBorder
        border.width: 1
        antialiasing: true
        clip: true

        height: inrow.height + divider.height + resultsArea.height
        enabled: sheet.open
        opacity: sheet.open ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        // OWN click-swallow (house doctrine): presses/wheel on the card never reach the
        // paper's double-click toggle. Child controls (declared after) sit on top.
        ReaderKeyboardArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: (w) => { w.accepted = true }
        }

        // ===== input row (mock .inrow) =====
        Item {
            id: inrow
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 50

            Image {
                id: searchIcon
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 17; height: 17
                sourceSize.width: 34; sourceSize.height: 34
                fillMode: Image.PreserveAspectFit
                smooth: true
                opacity: 0.40                       // inkFaint stroke (mock .inrow svg)
                source: Qt.resolvedUrl("../../assets/icons/reader2/search.svg")
            }

            TextInput {
                id: input
                objectName: "reader2SearchInput"
                anchors.left: searchIcon.right
                anchors.leftMargin: 12
                anchors.right: countLabel.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ink
                font.family: Theme.ui
                font.pixelSize: 14
                selectionColor: Theme.gold
                selectByMouse: true
                clip: true
                activeFocusOnTab: true
                KeyNavigation.tab: sheet.hasResults ? resList : input
                KeyNavigation.backtab: sheet.hasResults ? resList : input
                KeyNavigation.priority: KeyNavigation.BeforeItem
                Keys.priority: Keys.BeforeItem
                Keys.onTabPressed: function(event) {
                    if (sheet.hasResults) resList.forceActiveFocus(Qt.OtherFocusReason)
                    else input.forceActiveFocus(Qt.OtherFocusReason)
                    event.accepted = true
                }
                Keys.onBacktabPressed: function(event) {
                    if (sheet.hasResults) resList.forceActiveFocus(Qt.OtherFocusReason)
                    else input.forceActiveFocus(Qt.OtherFocusReason)
                    event.accepted = true
                }
                onAccepted: sheet.submitted(text.trim())
                Keys.onEscapePressed: sheet.closeRequested()

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: input.text.length === 0
                    text: "Search this book"
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 14
                }
            }

            Text {
                id: countLabel
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                visible: sheet.hasResults
                text: L.searchCountText(sheet.resultCount, sheet.capped)
                color: Theme.inkGhost
                font.family: Theme.ui
                font.pixelSize: 12
            }
        }

        // hairline under the input row (mock .inrow border-bottom)
        Rectangle {
            id: divider
            anchors.top: inrow.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.barBorder
        }

        // ===== results / empty placeholder (mock .results) =====
        Item {
            id: resultsArea
            anchors.top: divider.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            // content-sized, capped at the mock's 280px scroll region (+16 vertical padding).
            height: sheet.hasResults ? Math.min(resList.contentHeight + 16, 296) : 64

            // quiet empty state: "Type to search" before a search, "No results" after one.
            Text {
                anchors.centerIn: parent
                visible: !sheet.hasResults
                text: sheet.lastQuery === "" ? "Type to search" : "No results"
                color: Theme.inkGhost
                font.family: Theme.ui
                font.pixelSize: 13
            }

            ListView {
                id: resList
                objectName: "reader2SearchResults"
                anchors.fill: parent
                anchors.margins: 8
                visible: sheet.hasResults
                clip: true
                model: sheet.results
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                KeyNavigation.tab: input
                KeyNavigation.backtab: input
                KeyNavigation.priority: KeyNavigation.BeforeItem
                Accessible.role: Accessible.List
                Accessible.name: "Search results"
                Keys.priority: Keys.BeforeItem
                Keys.onTabPressed: function(event) { input.forceActiveFocus(Qt.OtherFocusReason); event.accepted = true }
                Keys.onBacktabPressed: function(event) { input.forceActiveFocus(Qt.OtherFocusReason); event.accepted = true }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) { sheet.closeRequested(); event.accepted = true; return }
                    searchKeys.handle(event)
                }

                delegate: Item {
                    id: res
                    required property var modelData
                    required property int index
                    width: resList.width
                    height: resCol.implicitHeight + 20

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 0
                        radius: 8
                        color: (resList.activeFocus && resList.currentIndex === res.index) ? Theme.goldWash
                                 : (resMa.containsMouse ? Theme.rowHover : "transparent")
                    }
                    Column {
                        id: resCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 4

                        // ghost-caps chapter label (mock .rwhere)
                        Text {
                            width: parent.width
                            visible: text !== ""
                            text: (res.modelData && res.modelData.chapterTitle) ? String(res.modelData.chapterTitle) : ""
                            elide: Text.ElideRight
                            color: Theme.inkGhost
                            font.family: Theme.ui
                            font.pixelSize: 11
                            font.letterSpacing: 1.4
                            font.capitalization: Font.AllUppercase
                        }
                        // serif excerpt; the matched substring is marked in GOLD (mock .rtext + mark).
                        // The gold token is supplied HERE (Theme.gold) â†’ the pure helper only
                        // escapes + assembles the StyledText.
                        Text {
                            width: parent.width
                            textFormat: Text.StyledText
                            text: '"' + L.searchRowStyled(res.modelData ? res.modelData.excerpt : null, Theme.gold) + '"'
                            wrapMode: Text.WordWrap
                            color: Theme.inkDim
                            font.family: "Literata"
                            font.pixelSize: 13
                        }
                    }
                    ReaderKeyboardArea {
                        id: resMa
                        anchors.fill: parent
                        keyboardTabStop: false
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sheet.resultActivated((res.modelData && res.modelData.cfi) ? String(res.modelData.cfi) : "")
                    }
                }
            }
        }
    }
    ReaderKeyboardCollectionController {
        id: searchKeys
        view: resList
        orientation: "vertical"
        count: resList.count
        onActivated: function(index) {
            var row = sheet.results && index >= 0 && index < sheet.results.length ? sheet.results[index] : null
            sheet.resultActivated(row && row.cfi ? String(row.cfi) : "")
        }
    }

}

