// BiblioLibraryPage — Biblio's Library tab (the Theatre-parity Library page, plan
// 2026-08-06-biblio-library-tab-theatre-parity.md, Slice 2). The book-domain mirror of
// LibraryPage.qml MINUS Theatre's video concepts: no watched/airing/finale/new-episode
// ledger — just search, a small filter (All | In Progress), sort (Recently added | Last read |
// A–Z), a wall of saved Collection entries, empty/no-match states, and a per-card ⋮ menu.
//
// One Collection entry → one card. Conservative Progress matching: a reliable match enables
// Resume; no match → the card's primary action is Details. Remove affects Collection
// membership only (never Progress, never files) — handled inline by the owner (BiblioWorld),
// not here. All derivations are BiblioLibraryApi (headless-proven in Slice 1); this page only
// paints and wires. Context properties (Collection/Progress) are typeof-guarded so it
// constructs offscreen for the harness.
import QtQuick
import QtQuick.Controls
import "BiblioLibraryApi.js" as Api

Item {
    id: root

    // ── page state ──
    property string sortMode: "added"          // added | lastRead | az
    property string stateFilter: ""            // "" | inProgress
    property string query: ""

    // ── reactive data (recompute on Collection / Progress change) ──
    property int collRev: (typeof Collection !== "undefined" ? Collection.revision : 0)
    property int progRev: (typeof Progress !== "undefined" ? Progress.revision : 0)
    property var allRows: computeRows(collRev, progRev)
    property int rowCount: allRows.length
    property var visibleRows: Api.sortBiblioRows(
        Api.applyBiblioFilters(allRows, { stateFilter: stateFilter, query: query }),
        sortMode)
    property int visibleCount: visibleRows.length

    // ── the floating ⋮ menu (rendered at root level; the wall GridView clips) ──
    property var menuRow: null
    property string menuRowId: ""
    property real menuX: 0
    property real menuY: 0

    // card menu → owning surface. resumeRequested carries the matched progress record;
    // detailRequested + removeRequested carry the original Collection entry.
    signal resumeRequested(var entry)
    signal detailRequested(var entry)
    signal removeRequested(var entry)

    Theme { id: theme }

    function computeRows(cr, pr) {
        if (typeof Collection === "undefined" || typeof Progress === "undefined") return []
        var entries = Collection.items("biblio")
        var plist = Progress.recent("book", 200)
        return Api.buildBiblioRows(entries, plist)
    }

    function toggleStateFilter(key) {
        stateFilter = (key === "" || stateFilter === key) ? "" : key
    }
    function openMenu(row, x, y) {
        if (menuRowId === row.entry.id) { closeMenu(); return }
        menuRow = row; menuRowId = row.entry.id; menuX = x; menuY = y
    }
    function closeMenu() { menuRow = null; menuRowId = "" }

    // Test seam (mirrors TankobanLibraryTab.handleCardTap): the menu items AND a direct card
    // primary-click both route through here, so the offscreen harness can exercise the action
    // logic without a real delegate/menu. action ∈ {"resume","detail","remove"}.
    function handleCardAction(row, action) {
        if (!row) return
        if (action === "resume" && row.canResume) { resumeRequested(row.progressRecord); return }
        if (action === "remove") { removeRequested(row.entry); return }
        detailRequested(row.entry)
    }

    // ── header: search + the small filter/sort bar ──
    Item {
        id: header
        objectName: "biblioLibraryHeader"
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.leftMargin: Math.max(64, theme.margin); anchors.rightMargin: Math.max(64, theme.margin)
        height: 76; z: 20

        TextField {
            id: searchField
            objectName: "biblioLibrarySearch"
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 250; height: 38; leftPadding: 16; rightPadding: 16
            placeholderText: "Search by title or author"; placeholderTextColor: theme.inkDimmer
            color: theme.ink; selectionColor: theme.gold; selectedTextColor: "#111111"
            font.family: theme.ui; font.pixelSize: 14
            background: Rectangle {
                radius: 19; color: Qt.rgba(0.03, 0.04, 0.06, 0.62)
                border.width: 1; border.color: searchField.activeFocus ? theme.gold : theme.edge
            }
            onTextEdited: root.query = text
            Keys.onEscapePressed: { text = ""; root.query = "" }
        }
    }

    // ── the quiet filter/sort bar ──
    Item {
        id: filterBar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: header.bottom
        height: 54; z: 19

        component BiblioFilterPill: Rectangle {
            property string label: ""
            property bool active: false
            signal picked()
            implicitWidth: pillText.implicitWidth + 30; height: 34; radius: 14
            color: active ? theme.gold : (pillHover.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
            border.width: active ? 0 : 1; border.color: Qt.rgba(1, 1, 1, 0.14)
            Behavior on color { ColorAnimation { duration: 130 } }
            Text {
                id: pillText; anchors.centerIn: parent; text: parent.label
                color: parent.active ? "#17120a" : theme.ink
                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            }
            HoverHandler { id: pillHover }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.picked() }
        }

        Row {
            anchors.left: parent.left; anchors.leftMargin: Math.max(64, theme.margin)
            anchors.verticalCenter: parent.verticalCenter; spacing: 8

            // sort
            Repeater {
                model: [ { key: "added", label: "Recently added" }, { key: "lastRead", label: "Last read" },
                         { key: "az", label: "A–Z" } ]
                delegate: BiblioFilterPill {
                    required property var modelData
                    label: modelData.label; active: root.sortMode === modelData.key
                    onPicked: root.sortMode = modelData.key
                }
            }
            Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: theme.edge }
            // state filter (All | In Progress). "Downloaded" omitted in v1 — no honest availability source.
            Repeater {
                model: [ { key: "", label: "All" }, { key: "inProgress", label: "In Progress" } ]
                delegate: BiblioFilterPill {
                    required property var modelData
                    label: modelData.label; active: root.stateFilter === modelData.key
                    onPicked: root.toggleStateFilter(modelData.key)
                }
            }
        }
    }

    // ── the wall ──
    GridView {
        id: wall
        objectName: "biblioLibraryGrid"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: filterBar.bottom; anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(48, theme.margin); anchors.rightMargin: Math.max(38, theme.margin - 10)
        anchors.topMargin: 22; anchors.bottomMargin: 18
        clip: true; boundsBehavior: Flickable.StopAtBounds
        model: root.visibleRows
        readonly property int columnCount: Math.max(2, Math.floor(width / 178))
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.floor((cellWidth - 16) * 1.5) + 72     // +72: title + author lines
        cacheBuffer: cellHeight * 2
        ScrollBar.vertical: HouseScrollBar { flick: wall }
        onContentYChanged: root.closeMenu()

        delegate: Item {
            id: card
            objectName: "biblioLibraryCard_" + (modelData.entry.id || "")
            required property var modelData
            required property int index
            width: wall.cellWidth - 16; height: wall.cellHeight - 18; x: 8
            readonly property real coverH: (wall.cellWidth - 16) * 1.5

            Rectangle {
                id: cover
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: card.coverH; radius: 6; clip: true; color: "#1b1d22"
                border.width: 1; border.color: cardHover.hovered ? theme.gold : theme.edge
                Behavior on border.color { ColorAnimation { duration: 130 } }

                // placeholder (title text) under any art — keeps the card legible before cover decodes
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#33405c" }
                        GradientStop { position: 1; color: "#12161f" }
                    }
                    Text {
                        anchors.centerIn: parent; width: parent.width - 24
                        text: card.modelData.entry.title || "Untitled"
                        color: Qt.rgba(1, 1, 1, 0.8); font.family: theme.display
                        font.pixelSize: 17; font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        maximumLineCount: 4; elide: Text.ElideRight
                    }
                }
                // cover art via the shared fallback-stack primitive — never a raw page-local Image
                Image {
                    anchors.fill: parent
                    source: card.modelData.cover || ""
                    fillMode: Image.PreserveAspectCrop; verticalAlignment: Image.AlignTop
                    asynchronous: true; cache: true
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                }

                // in-progress hairline — cover base
                Rectangle {
                    visible: card.modelData.progress > 0
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 3; color: Qt.rgba(1, 1, 1, 0.16)
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: parent.width * Math.max(0, Math.min(1, card.modelData.progress)); color: theme.gold
                    }
                }
            }

            // ⋮ button — painted visible on every card (not hover-gated) so it is addressable by
            // name (Lanista ui-click, harness). The plan's recommended menu shape (a); hover-reveal
            // shape (b) would downgrade menu-Remove to Test-reported because the bridge has no hover.
            Rectangle {
                id: dots
                objectName: "biblioLibraryCardMenu_" + (card.modelData.entry.id || "")
                anchors.right: cover.right; anchors.top: cover.top; anchors.margins: 8
                width: 26; height: 26; radius: 8; color: Qt.rgba(0.04, 0.04, 0.075, 0.82)
                border.width: 1
                border.color: root.menuRowId === card.modelData.entry.id ? theme.gold : theme.edge
                Text { anchors.centerIn: parent; text: "⋮"; color: theme.ink; font.pixelSize: 15 }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var pt = dots.mapToItem(root, dots.width, dots.height)
                        root.openMenu(card.modelData, pt.x, pt.y)
                    }
                }
            }

            // title (primary line)
            Text {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: cover.bottom; anchors.topMargin: 9
                text: card.modelData.entry.title || "Untitled"
                color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                elide: Text.ElideRight; maximumLineCount: 1
            }
            // author (secondary line — the biblio-specific card difference vs Theatre's video card)
            Text {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: cover.bottom; anchors.topMargin: 28
                text: card.modelData.author || ""
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                elide: Text.ElideRight; maximumLineCount: 1
            }

            HoverHandler { id: cardHover }
            MouseArea {
                anchors.fill: cover; cursorShape: Qt.PointingHandCursor
                // primary click: Resume when a reliable match exists, else Details (plan §8)
                onClicked: root.handleCardAction(card.modelData,
                                                 card.modelData.canResume ? "resume" : "detail")
            }
        }
    }
    ScrollGlide { flick: wall }

    // ── empty states ──
    Column {
        objectName: "biblioLibraryEmptyState"
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Your library is empty"
            color: theme.ink; font.family: theme.display; font.pixelSize: 30
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Save a book with + Library — it lands here."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
        }
    }
    Column {
        objectName: "biblioLibraryNoMatchState"
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length > 0 && root.visibleRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Nothing matches"
            color: theme.ink; font.family: theme.display; font.pixelSize: 30
        }
    }

    // ── the floating ⋮ menu (root level — the wall clips) ──
    MouseArea {
        anchors.fill: parent; z: 55; visible: root.menuRow !== null
        onClicked: root.closeMenu()
    }
    Rectangle {
        id: menuPanel
        objectName: "biblioLibraryMenuPanel"
        z: 56; visible: root.menuRow !== null
        width: 200; radius: 13
        color: Qt.rgba(0.043, 0.047, 0.075, 0.98); border.width: 1; border.color: theme.edge
        implicitHeight: menuCol.implicitHeight + 12
        height: implicitHeight
        x: Math.max(8, Math.min(root.menuX - width, root.width - width - 8))
        y: Math.max(8, Math.min(root.menuY, root.height - height - 8))
        MouseArea { anchors.fill: parent }   // click-swallower body

        component BiblioMenuItem: Item {
            property string label: ""
            property string objectBase: ""     // the menu item's objectName stem
            property bool warn: false
            signal picked()
            width: parent ? parent.width : 0; height: 38
            Rectangle {
                anchors.fill: parent; anchors.margins: 2; radius: 9
                color: miHover.hovered ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 14
                text: parent.label
                color: parent.warn ? "#e08a8a" : theme.ink
                font.family: theme.ui; font.pixelSize: 13
            }
            HoverHandler { id: miHover }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.picked() }
        }

        Column {
            id: menuCol
            anchors.top: parent.top; anchors.topMargin: 6
            anchors.left: parent.left; anchors.right: parent.right
            BiblioMenuItem {
                objectName: "biblioLibraryMenuItem_resume"
                label: "Resume"
                visible: root.menuRow && root.menuRow.canResume
                height: visible ? 38 : 0
                onPicked: { if (root.menuRow) root.handleCardAction(root.menuRow, "resume"); root.closeMenu() }
            }
            BiblioMenuItem {
                objectName: "biblioLibraryMenuItem_details"
                label: "Details"
                onPicked: { if (root.menuRow) root.handleCardAction(root.menuRow, "detail"); root.closeMenu() }
            }
            Rectangle { width: parent.width - 16; x: 8; height: 1; color: theme.edge }
            BiblioMenuItem {
                objectName: "biblioLibraryMenuItem_remove"
                label: "Remove from Library"; warn: true
                onPicked: { if (root.menuRow) root.handleCardAction(root.menuRow, "remove"); root.closeMenu() }
            }
        }
    }
}
