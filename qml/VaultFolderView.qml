// VaultFolderView — the Vault's file-first detail surface (Slice 13), built to the locked mock
// agents/colosseum-vault-folder-view-mock.html. Click a shelf tile → this surface: a sticky
// preview pane (cover/gradient, kind eyebrow, title, disk facts, doors) on the left, and on the
// right the folder's REAL files exactly as they sit on disk — cleaned titles with the real
// filename faint beneath, real subfolders as group headers, natural order, a gold progress
// hairline. Identification decorates the pane (later slices); it never restructures the list.
//
// A SEEDABLE component (like VaultConfirmCard / OpenRecentPanel): it owns no backend. It takes a
// flat `model` of file rows (VaultLibrary.items(kind, seriesKey)) + `facts` for the pane, and emits
// backRequested / revealRequested(path) / openRequested(row) / continueRequested — VaultPage wires
// those to navigation, the Reveal invocable, and (Slice 14) LocalLaunch. So a Qt Quick Test drives
// it with a seeded model, no app.
//
// Row shape (VaultIndex::filesInSubtree): { id, path, displayTitle, realName, subfolder, kind,
// size, mtimeMs, pages, durationSec, author, format, progressed, coverRef }. progress % and
// last-read time come from Progress (Slice 14), not the index — so the hairline and the last-read
// sort stay coarse (progressed tick only) until reads are wired.
import QtQuick
import QtQuick.Controls

Item {
    id: view
    objectName: "vaultFolderView"
    anchors.fill: parent

    // ── inputs ──
    property var model: []                 // flat file rows in natural (subfolder, sortKey) order
    property string title: ""
    property string kind: "comic"          // comic | book | video
    property string coverUrl: ""           // series cover (image://comiccover/…) or "" → gradient
    property string rootPath: ""           // the series/show folder on disk
    property string identityId: ""
    property string identitySource: ""
    property string identityWorld: ""
    property string synopsis: ""
    property string synopsisSource: ""
    readonly property bool worldDoorReady: identityId.length > 0 && identityWorld.length > 0
    readonly property bool hasSynopsis: synopsis.length > 0
    property Item backdrop: null
    // ── outputs ──
    signal backRequested()
    signal revealRequested(string path)
    signal openRequested(var row)
    signal continueRequested()
    signal viewWorldRequested(var identity)

    property string sortMode: "natural"    // natural | alpha | newest | lastread
    property bool sortOpen: false

    // Preserve the internal scroll across a live model re-join: VaultPage re-joins Progress under an
    // occluded folder view (read a file → Back), and a naive model swap can collapse contentHeight
    // mid-rebuild and clamp contentY to 0. A same-folder re-join keeps the row count, so restoring the
    // captured offset is safe. (Advisor-flagged, Slice 14.)
    onModelChanged: {
        var y = listFlick.contentY
        if (y > 0) Qt.callLater(function () {
            listFlick.contentY = Math.max(0, Math.min(y, listFlick.contentHeight - listFlick.height))
        })
    }

    Theme { id: theme }

    // ── Lanista/plan contract ──
    readonly property int fileCount: model ? model.length : 0
    readonly property int groupCount: {
        var seen = ({}), n = 0
        for (var i = 0; i < fileCount; i++) {
            var sf = model[i].subfolder || ""
            if (sf.length && !seen[sf]) { seen[sf] = true; n++ }
        }
        return n
    }
    readonly property int readCount: {
        var n = 0
        for (var i = 0; i < fileCount; i++) if (model[i].progressed) n++
        return n
    }
    readonly property double totalBytes: {
        var b = 0
        for (var i = 0; i < fileCount; i++) b += (model[i].size || 0)
        return b
    }

    function humanSize(bytes) {
        if (bytes <= 0) return "0 B"
        var u = ["B", "KB", "MB", "GB", "TB"], i = 0, v = bytes
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return (v >= 10 ? Math.round(v) : Math.round(v * 10) / 10) + " " + u[i]
    }
    function kindLabel(k) { return k === "comic" ? "COMICS" : k === "book" ? "BOOKS" : "VIDEO" }
    function fileIcon(k) {
        return k === "book" ? "../assets/icons/book-library.svg"
             : k === "video" ? "../assets/icons/projector-theatre.svg"
             : "../assets/icons/comic-book.svg"
    }
    function metaFor(row) {
        if (row.kind === "comic") return (row.pages > 0 ? row.pages + " pages" : "")
        if (row.kind === "video") return (row.durationSec > 0
            ? Math.floor(row.durationSec / 3600) + "h " + Math.floor((row.durationSec % 3600) / 60) + "m"
            : (row.format || "").toUpperCase())
        return (row.format || "").toUpperCase()   // book
    }
    function dateFor(row) {
        return row.mtimeMs > 0 ? Qt.formatDate(new Date(row.mtimeMs), "MMM yyyy") : ""
    }
    function sortLabel(m) {
        return m === "alpha" ? "Alphabetical" : m === "newest" ? "Newest"
             : m === "lastread" ? "Last read" : "Natural order"
    }

    // Build the display list: natural mode groups by real subfolder (loose first, no header, then
    // SUBFOLDER\ headers); other sorts flatten to a single reordered list. fileIndex numbers only
    // FILE rows (the vaultFileRow_<n> contract), not the group headers.
    function displayItems() {
        var rows = model ? model.slice() : []
        if (view.sortMode === "alpha")
            rows.sort(function (a, b) { return ("" + (a.displayTitle || "")).localeCompare("" + (b.displayTitle || "")) })
        else if (view.sortMode === "newest")
            rows.sort(function (a, b) { return (b.mtimeMs || 0) - (a.mtimeMs || 0) })
        else if (view.sortMode === "lastread")   // real last-read time (Slice 14 join); unread sink to the bottom
            rows.sort(function (a, b) { return (Number(b.lastReadMs) || 0) - (Number(a.lastReadMs) || 0) })

        var out = [], fi = 0
        if (view.sortMode === "natural") {
            var cur = null
            for (var i = 0; i < rows.length; i++) {
                var sf = rows[i].subfolder || ""
                if (sf !== cur) { cur = sf; if (sf.length) out.push({ header: true, label: sf }) }
                out.push({ header: false, row: rows[i], fileIndex: fi++ })
            }
        } else {
            for (var j = 0; j < rows.length; j++)
                out.push({ header: false, row: rows[j], fileIndex: fi++ })
        }
        return out
    }

    // dark ground + the same live wallpaper the other Vault surfaces use. The ground stays
    // passive so the named doors below receive real window input events.
    Rectangle { anchors.fill: parent; color: "#000000" }
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent; sourceItem: view.backdrop; live: true
            hideSource: false; visible: view.backdrop !== null
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.92) }
    }

    BackAction {
        variant: "capsule"; tip: "Back"
        anchors.top: parent.top; anchors.left: parent.left
        anchors.topMargin: 21; anchors.leftMargin: theme.margin - 10
        onTriggered: view.backRequested()
    }

    // ── the split: fixed preview pane left, scrollable file list right ──
    Item {
        id: split
        anchors.fill: parent
        anchors.topMargin: 84
        anchors.leftMargin: theme.margin
        anchors.rightMargin: theme.margin
        anchors.bottomMargin: 24

        // LEFT — the preview pane (fixed; does not scroll with the list)
        Column {
            id: pane
            width: 320
            anchors.top: parent.top
            anchors.left: parent.left
            spacing: 0

            Rectangle {   // art / gradient
                width: 300; height: 220; radius: 16; clip: true
                border.width: 1; border.color: theme.edge
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0.16, 0.14, 0.20, 1) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.055, 0.060, 0.090, 1) }
                }
                Image {
                    anchors.fill: parent
                    visible: !!view.coverUrl
                    source: view.coverUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: true
                }
                Image {
                    anchors.centerIn: parent; width: 48; height: 48; opacity: 0.4
                    visible: !view.coverUrl
                    source: view.fileIcon(view.kind); fillMode: Image.PreserveAspectFit
                }
            }

            Text {
                topPadding: 22
                text: view.kindLabel(view.kind) + " · VAULT"
                color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3
            }
            Text {
                topPadding: 6; width: pane.width
                text: view.title
                color: theme.ink; font.family: theme.display; font.pixelSize: 38
                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
            }
            Text {
                topPadding: 12; width: pane.width; wrapMode: Text.WordWrap; lineHeight: 1.5
                text: view.fileCount + (view.fileCount === 1 ? " file" : " files")
                      + "  ·  " + view.humanSize(view.totalBytes)
                      + (view.readCount > 0 ? "  ·  read " + view.readCount : "")
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
            }
            Text {
                topPadding: 4; width: pane.width; elide: Text.ElideMiddle
                text: view.rootPath
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
            }

            // ── doors ── (Continue/Read/Play open in Slice 14; Reveal works now; View in <world> S17)
            Column {
                topPadding: 22
                spacing: 10
                width: pane.width

                Rectangle {
                    objectName: "vaultFolderContinue"
                    width: pane.width; height: 46; radius: 12
                    color: primaryMa.containsMouse ? Qt.rgba(0.98, 0.82, 0.36, 1) : theme.gold
                    Text {
                        anchors.centerIn: parent
                        text: view.readCount > 0 ? "Continue"
                            : view.kind === "video" ? "Play" : "Read"
                        color: "#151310"; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: primaryMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: view.readCount > 0 ? view.continueRequested()
                                 : (view.model && view.model.length ? view.openRequested(view.model[0]) : null)
                    }
                }
                Rectangle {   // View in <world> — identity-gated (S17)
                    objectName: "vaultFolderViewWorld"
                    width: pane.width; height: 44; radius: 12
                    enabled: true
                    color: view.worldDoorReady && worldMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    border.width: 1; border.color: theme.edge; opacity: view.worldDoorReady ? 1.0 : 0.45
                    Text {
                        anchors.centerIn: parent
                        text: "View in " + (view.identityWorld || (view.kind === "book" ? "Biblio" : view.kind === "video" ? "Theatre" : "Tankoban"))
                        color: view.worldDoorReady ? theme.ink : theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                    }
                    MouseArea {
                        id: worldMa; anchors.fill: parent; enabled: true; hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: if (view.worldDoorReady) {
                            view.viewWorldRequested({
                                identityId: view.identityId, source: view.identitySource,
                                world: view.identityWorld, title: view.title,
                                synopsis: view.synopsis
                            })
                        }
                    }
                }
                Rectangle {   // Reveal in Explorer — live now
                    objectName: "vaultFolderReveal"
                    width: pane.width; height: 44; radius: 12
                    color: revealMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    border.width: 1; border.color: theme.edge
                    Text {
                        anchors.centerIn: parent; text: "Reveal in Explorer"
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                    }
                    MouseArea {
                        id: revealMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: view.revealRequested(view.rootPath)
                    }
                }
            }
            Text {
                objectName: "vaultFolderSynopsis"
                visible: view.synopsis.length > 0
                topPadding: 14; width: pane.width; wrapMode: Text.WordWrap
                text: view.synopsis
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; lineHeight: 1.35
                maximumLineCount: 4; elide: Text.ElideRight
            }
            Text {
                objectName: "vaultFolderSynopsisSource"
                visible: view.synopsis.length > 0 && view.synopsisSource.length > 0
                topPadding: 5; width: pane.width
                text: "Source: " + view.synopsisSource
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10
            }
        }

        // RIGHT — the file list (the folder's truth), scrollable
        Item {
            anchors.top: parent.top
            anchors.left: pane.right; anchors.leftMargin: 36
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            // list head + sort control
            Item {
                id: listHead
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                height: 40
                Text {
                    id: filesLabel
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    text: "Files"; color: theme.ink; font.family: theme.display; font.pixelSize: 26
                }
                Rectangle {
                    id: sortBtn
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    width: sortText.implicitWidth + 34; height: 32; radius: 8
                    color: sortMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    border.width: 1; border.color: theme.edge
                    Text {
                        id: sortText; anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                        text: view.sortLabel(view.sortMode) + "  ▾"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                    }
                    MouseArea { id: sortMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: view.sortOpen = !view.sortOpen }

                    Rectangle {   // the sort menu
                        visible: view.sortOpen
                        z: 60
                        anchors.top: parent.bottom; anchors.topMargin: 6; anchors.right: parent.right
                        width: 160; height: sortCol.implicitHeight + 10; radius: 10
                        color: Qt.rgba(0.071, 0.082, 0.110, 0.99); border.width: 1; border.color: theme.edge
                        Column {
                            id: sortCol; width: parent.width; padding: 5
                            Repeater {
                                model: [ { k: "natural", l: "Natural order" }, { k: "alpha", l: "Alphabetical" },
                                         { k: "newest", l: "Newest" }, { k: "lastread", l: "Last read" } ]
                                delegate: Rectangle {
                                    required property var modelData
                                    width: parent.width - 10; height: 32; radius: 7
                                    color: optMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 12
                                        text: modelData.l
                                        color: view.sortMode === modelData.k ? theme.gold : theme.ink
                                        font.family: theme.ui; font.pixelSize: 13
                                    }
                                    MouseArea {
                                        id: optMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: { view.sortMode = modelData.k; view.sortOpen = false }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Flickable {
                id: listFlick
                anchors.top: listHead.bottom; anchors.topMargin: 14
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                contentWidth: width; contentHeight: rowsCol.implicitHeight + 20
                clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: HouseScrollBar { flick: listFlick }

                Column {
                    id: rowsCol
                    width: listFlick.width
                    spacing: 8
                    Repeater {
                        model: view.displayItems()
                        delegate: Loader {
                            required property var modelData
                            width: rowsCol.width
                            sourceComponent: modelData.header ? groupHeader : fileRow
                            property var itemData: modelData
                        }
                    }
                }
            }
        }
    }

    // group header ("SEASON 01\")
    Component {
        id: groupHeader
        Item {
            height: 34
            property string label: parent.itemData.label
            Text {
                id: gh
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                text: (parent.label + "\\").toUpperCase()
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.4
            }
            Rectangle {
                anchors.left: gh.right; anchors.leftMargin: 12; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter; height: 1; color: theme.edge
            }
        }
    }

    // one file row
    Component {
        id: fileRow
        Rectangle {
            id: rowRect
            property var row: parent.itemData.row
            property int fileIndex: parent.itemData.fileIndex
            objectName: "vaultFileRow_" + fileIndex
            // row contract reads
            property string displayTitle: row.displayTitle || ""
            property string realName: row.realName || ""
            property bool progressed: !!row.progressed
            property bool away: !!row.away
            property string errorState: row.errorState || ""
            property string errorDetail: row.errorDetail || row.admissionDetail || ""

            height: 76; radius: 12
            color: rowMa.containsMouse ? Qt.rgba(0.11, 0.13, 0.17, 0.98) : Qt.rgba(0.094, 0.110, 0.145, 0.94)
            border.width: 1; border.color: rowRect.progressed ? Qt.rgba(0.78, 0.62, 0.29, 0.5) : theme.edge
            opacity: rowRect.away ? 0.52 : 1.0
            clip: true

            Row {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 18
                spacing: 16

                Rectangle {   // thumbnail
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 60; radius: 6; clip: true
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.16, 0.14, 0.20, 1) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.055, 0.060, 0.090, 1) }
                    }
                    Image {   // per-file cover URL supplied by the backend (VaultLibrary.items) when present
                        anchors.fill: parent
                        visible: !!rowRect.row.coverUrl
                        source: rowRect.row.coverUrl || ""
                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                    }
                    Image {
                        anchors.centerIn: parent; width: 18; height: 18; opacity: 0.4
                        visible: !rowRect.row.coverUrl
                        source: view.fileIcon(rowRect.row.kind); fillMode: Image.PreserveAspectFit
                    }
                }

                Column {   // name block — cleaned title + faint real filename
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 44 - 16 - metaBlock.width - 34 - 32
                    spacing: 3
                    Text {
                        width: parent.width; text: rowRect.displayTitle
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width; text: rowRect.realName
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; elide: Text.ElideMiddle
                    }
                }

                Column {   // meta: pages/duration/format + size + date
                    id: metaBlock
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        anchors.right: parent.right; text: view.metaFor(rowRect.row)
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; visible: text.length > 0
                    }
                    Text {
                        anchors.right: parent.right
                        text: rowRect.away ? "Unavailable" : rowRect.errorState ? "Needs attention" : ""
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                        visible: text.length > 0
                    }
                    Text {
                        anchors.right: parent.right
                        text: view.humanSize(rowRect.row.size || 0) + "   ·   " + view.dateFor(rowRect.row)
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                    }
                }

                Text {   // read tick — truthful only; real progress %/last-read join lands in Slice 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34; horizontalAlignment: Text.AlignHCenter
                    text: rowRect.progressed ? "✓" : ""
                    color: theme.gold
                    font.family: theme.ui; font.pixelSize: 15
                }
            }

            MouseArea {
                id: rowMa; anchors.fill: parent; hoverEnabled: true
                enabled: !rowRect.away && !rowRect.errorState
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: view.openRequested(rowRect.row)
            }

            Rectangle {
                anchors.fill: parent
                visible: rowRect.away || !!rowRect.errorState
                color: Qt.rgba(0.04, 0.04, 0.04, rowRect.away ? 0.28 : 0.18)
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 76
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowRect.away ? "Unavailable" : (rowRect.errorDetail || "Needs attention")
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                    elide: Text.ElideRight; width: parent.width - 180
                }
            }

            // gold progress hairline — the real read position from Progress (the Slice 14 join;
            // comic page fraction / book fraction / video position). Hidden until a file carries
            // progress; degrades to nothing on a seeded test model with no progressFraction field.
            Rectangle {
                visible: (rowRect.row.progressFraction || 0) > 0
                anchors.left: parent.left; anchors.bottom: parent.bottom
                anchors.leftMargin: 1; anchors.bottomMargin: 1
                height: 2; radius: 1
                width: (parent.width - 2) * Math.max(0, Math.min(1, rowRect.row.progressFraction || 0))
                color: theme.gold
            }
        }
    }
}
