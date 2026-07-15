// ComicTorrentSourcesPage — the full-screen "Find alternate sources" picker for a
// collected edition, in the Colosseum house language (the SourcesSheet visual
// stack: black base, key-art hero washing down, gold eyebrow + Fraunces title,
// a glass result table). Comics-specific: an edition identity rail (canonical
// title / ISBN / collected range) and per-row matched-clue evidence so the user
// sees WHY a result ranks where it does. The user always chooses the torrent;
// weak matches require an explicit confirmation; ambiguous packs open the
// second-stage archive picker. Nothing here auto-picks.
//
// Belongs to ComicSeriesPage (lazy in practice — ComicSeriesPage is lazy-loaded);
// never touches root startup. All acquisition rides the global Comics object
// under the original ledger chId — this page emits no reader signal.
import QtQuick
import QtQuick.Controls

Item {
    id: sheet
    anchors.fill: parent

    property var comicsApi: typeof Comics !== "undefined" ? Comics : null
    property Item backdrop: null
    property var context: ({})
    property var rows: []
    property var errors: []
    property var archiveFiles: []          // ambiguous: eligible candidates
    property var missingIssues: []         // incomplete: named missing issues
    property var combinedFiles: []         // combined: the whole-archive candidate
    property string queryText: ""
    property bool open: false
    property bool loading: false
    property bool complete: false
    property bool confirmingWeak: false
    property bool acquiring: false        // a torrent has been chosen and is being acquired
    property var pendingRow: null
    // ONE typed-state driver for everything past "a torrent was tapped":
    //   results     — browsing/query, nothing chosen yet (also the confirmingWeak gate)
    //   inspecting  — automatic pack path is resolving metadata / just resumed after a choice
    //   ambiguous   — two+ eligible files, user must pick (ComicTorrentArchivePicker)
    //   incomplete  — the pack is missing required issues; no auto-download
    //   combined    — only a combined multi-edition archive covers this edition
    property string selectionState: "results"
    readonly property var visibleRows: rows
    readonly property string identityLine: buildIdentityLine()

    signal closed()

    visible: sheet.open || sheet.opacity > 0.01
    opacity: sheet.open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Theme { id: theme }

    // ── state contract ───────────────────────────────────────────────────────
    function buildIdentityLine() {
        var parts = []
        if (context.editionTitle) parts.push(String(context.editionTitle))
        if (context.isbn) parts.push("ISBN " + String(context.isbn))
        if (context.collects) parts.push(String(context.collects))
        return parts.join("      ·      ")
    }

    function show(contextObject) {
        context = contextObject
        rows = []; errors = []; archiveFiles = []; missingIssues = []; combinedFiles = []
        queryText = ""
        loading = true; complete = false
        confirmingWeak = false; selectionState = "results"; acquiring = false; pendingRow = null
        open = true
        if (comicsApi)
            comicsApi.searchTorrentSources(context.issueId, context.seriesTitle,
                context.editionTitle, context.isbn, context.collects)
    }

    function hide() {
        if (comicsApi && context.issueId) {
            comicsApi.cancelTorrentSourceSearch(context.issueId)
            // Backing out of a live acquisition (resolving/choosing/downloading/
            // assembling — anything past "results", not yet handed off to the
            // ledger) tears down the torrent + temp files too. Exactly one call:
            // any handler that closes the page on a SAFE outcome clears
            // `acquiring` itself before calling hide().
            if (acquiring) comicsApi.cancelDownload(context.issueId)
        }
        open = false; rows = []; pendingRow = null
        confirmingWeak = false; selectionState = "results"; acquiring = false
        archiveFiles = []; missingIssues = []; combinedFiles = []
        closed()
    }

    function submitManualQuery() {
        if (!queryText || !queryText.trim().length) return
        rows = []; errors = []; loading = true; complete = false
        confirmingWeak = false; pendingRow = null
        if (comicsApi) comicsApi.searchTorrentSourcesQuery(context.issueId, queryText)
    }

    function applySources(issueId, newRows, isComplete) {
        if (issueId !== context.issueId) return   // stale handle from a replaced search
        rows = newRows; complete = isComplete; loading = !isComplete
    }

    function applyFailure(issueId, reason) {
        if (issueId !== context.issueId) return
        rows = []; complete = true; loading = false; errors = [String(reason)]
    }

    function selectRow(row) {
        pendingRow = row
        if (row && row.confidence === "weak") { confirmingWeak = true; return }
        confirmWeakSelection()
    }

    function confirmWeakSelection() {
        confirmingWeak = false
        if (!comicsApi || !pendingRow) return
        comicsApi.cancelTorrentSourceSearch(context.issueId)
        beginAutomaticDownload(pendingRow)
    }

    function cancelWeakSelection() { confirmingWeak = false; pendingRow = null }

    // Tap a coverage/identity-matched result -> auto isolate + download the
    // edition (the shared-infohash pack transport), NO manual file pick. The
    // CANONICAL edition title/isbn/collects are the identity the transport
    // matches against; the row's release title/magnet are display/source only.
    function beginAutomaticDownload(row) {
        acquiring = true
        selectionState = "inspecting"
        comicsApi.downloadTorrentEdition(context.issueId, context.seriesId, context.seriesTitle,
            context.editionTitle, context.isbn, context.collects, row.infoHash, row.magnetUri)
    }

    // ── typed outcomes from the automatic pack path ──────────────────────────

    function applyArchiveChoices(issueId, files) {
        if (issueId !== context.issueId) return
        archiveFiles = files; selectionState = "ambiguous"
    }

    function chooseArchive(index) {
        if (comicsApi) comicsApi.chooseTorrentFiles(context.issueId, [index])
        selectionState = "inspecting"   // resumes; closes on the next safe progress/finish
    }

    function applyIncomplete(issueId, missing) {
        if (issueId !== context.issueId) return
        missingIssues = missing || []; selectionState = "incomplete"
    }

    // "Try another source" and "Search manually" both give up on this pack —
    // there is no backend capability to hand-pick individual files out of an
    // incomplete set — and return to browsing; the manual variant also focuses
    // the query field so the user can type a more complete release right away.
    function rejectIncomplete(focusManualQuery) {
        if (comicsApi) comicsApi.cancelDownload(context.issueId)
        acquiring = false; selectionState = "results"; missingIssues = []
        if (focusManualQuery) queryInput.forceActiveFocus()
    }

    function applyCombined(issueId, files) {
        if (issueId !== context.issueId) return
        combinedFiles = files || []; selectionState = "combined"
    }

    function confirmCombined() {
        if (comicsApi) comicsApi.confirmCombinedArchive(context.issueId)
        selectionState = "inspecting"   // resumes; closes on the next safe progress/finish
    }

    function rejectCombined() {
        if (comicsApi) comicsApi.cancelDownload(context.issueId)
        acquiring = false; selectionState = "results"; combinedFiles = []
    }

    // A safe, unique auto-decision (or a resumed manual choice) has started
    // moving bytes — hand off to the normal ledger-row progress and close.
    // Cleared BEFORE hide() so its cancel-on-Back guard does not fire.
    function closeOnSafeProgress(issueId) {
        if (issueId !== context.issueId) return
        if (selectionState !== "inspecting") return
        acquiring = false
        hide()
    }

    // Any typed failure while a pack attempt is live (TargetMissing,
    // UnsupportedPayload, an assembly error after a manual choice, …) — never
    // leave the user stuck mid-state; fall back to browsing the same results.
    function applyAcquisitionFailure(issueId) {
        if (issueId !== context.issueId) return
        if (selectionState === "results") return
        acquiring = false; selectionState = "results"
    }

    function confidenceColor(c) {
        if (c === "strong") return theme.gold
        if (c === "possible") return "#8ea3c0"   // muted blue-grey
        return "#c98a8a"                          // muted red — weak, still visible
    }
    function confidenceLabel(c) {
        if (c === "strong") return "STRONG MATCH"
        if (c === "possible") return "POSSIBLE MATCH"
        return "WEAK MATCH"
    }

    // ── facade signals: guarded by the live edition id (stale handles ignored) ──
    Connections {
        target: sheet.comicsApi
        ignoreUnknownSignals: true
        function onTorrentSourcesUpdated(issueId, rows, complete) { sheet.applySources(issueId, rows, complete) }
        function onTorrentSourceSearchFailed(issueId, reason) { sheet.applyFailure(issueId, reason) }
        // Reused for the pack transport's Ambiguous case too (same "pick one of
        // these files" shape — see ComicTorrentDownloader.h).
        function onTorrentArchiveSelectionRequired(issueId, files) { sheet.applyArchiveChoices(issueId, files) }
        function onTorrentCombinedArchiveConfirmationRequired(issueId, files) { sheet.applyCombined(issueId, files) }
        function onTorrentIncompleteIssueSetDetected(issueId, missingIssues) { sheet.applyIncomplete(issueId, missingIssues) }
        // A safe, unique auto-decision (or a resumed manual choice) has begun
        // moving bytes, or has already finished outright — either way the
        // ledger row owns it now; close without cancelling.
        function onProgress(issueId, done, total) { sheet.closeOnSafeProgress(issueId) }
        function onFinished(issueId) { sheet.closeOnSafeProgress(issueId) }
        function onFailed(issueId, reason) { sheet.applyAcquisitionFailure(issueId) }
    }

    // ── base: float over the wallpaper, not a flat void ──
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: sheet.backdrop
        live: true; hideSource: false
        visible: sheet.backdrop !== null
        opacity: 0.5
    }
    MouseArea { anchors.fill: parent }   // absorb clicks from below

    // ── key-art hero across the top, washing down ──
    Item {
        id: bannerStrip
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 300
        Image {
            anchors.fill: parent
            source: sheet.context.cover ? sheet.context.cover : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            visible: source != ""
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.25) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.5) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.92) }
            }
        }
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: bannerStrip.bottom; anchors.bottom: parent.bottom
        color: Qt.rgba(0, 0, 0, 0.9)
    }

    BackAction {
        x: theme.margin; y: 30; z: 20
        onTriggered: sheet.hide()
    }

    // ── title block + edition identity rail, pinned to the bottom of the banner ──
    Column {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: parent.top; anchors.topMargin: bannerStrip.height - height - 26
        spacing: 12
        Text {
            width: parent.width
            text: "ALTERNATE SOURCES · COLLECTED EDITION"
            color: theme.gold; font.family: theme.ui; font.pixelSize: 12
            font.letterSpacing: 4; elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: sheet.context.editionTitle ? String(sheet.context.editionTitle)
                  : (sheet.context.seriesTitle ? String(sheet.context.seriesTitle) : "Alternate sources")
            color: theme.ink; font.family: theme.display
            font.pixelSize: 56; font.weight: Font.DemiBold
            maximumLineCount: 1; elide: Text.ElideRight
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
        Text {
            width: parent.width
            visible: sheet.identityLine.length > 0
            text: sheet.identityLine
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
            font.letterSpacing: 1; elide: Text.ElideRight
        }
    }

    // ── editable query + Search ──
    Item {
        id: queryBar
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: bannerStrip.bottom; anchors.topMargin: 20
        height: 56
        visible: sheet.selectionState === "results"

        Rectangle {
            id: queryField
            anchors.left: parent.left
            anchors.right: searchBtn.left; anchors.rightMargin: 14
            height: parent.height; radius: 14
            color: Qt.rgba(1, 1, 1, 0.06)
            border.width: 1
            border.color: queryInput.activeFocus ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : theme.edge
            Behavior on border.color { ColorAnimation { duration: 140 } }
            TextInput {
                id: queryInput
                anchors.fill: parent
                anchors.leftMargin: 22; anchors.rightMargin: 22
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: theme.ink; font.family: theme.ui; font.pixelSize: 16
                selectByMouse: true
                selectionColor: Qt.rgba(0.94, 0.77, 0.29, 0.35)
                text: sheet.queryText
                onTextEdited: sheet.queryText = text
                onAccepted: sheet.submitManualQuery()
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: queryInput.text.length === 0
                    text: "Search another title or ISBN"
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 16
                }
            }
        }
        Rectangle {
            id: searchBtn
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 120; height: 44; radius: 14
            color: sbMa.containsMouse ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.85)
            Behavior on color { ColorAnimation { duration: 140 } }
            Text {
                anchors.centerIn: parent; text: "Search"; color: "#1a1306"
                font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
            }
            MouseArea {
                id: sbMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: sheet.submitManualQuery()
            }
        }
    }

    // ── the glass table: results OR the archive picker ──
    Glass {
        id: table
        backdrop: sheet.backdrop
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: queryBar.bottom; anchors.topMargin: 16
        anchors.bottom: parent.bottom; anchors.bottomMargin: 26
        radius: 18
        track: 0

        // ---- results view ----
        Item {
            anchors.fill: parent
            visible: sheet.selectionState === "results"

            Item {
                id: tableHead
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 52
                visible: sheet.visibleRows.length > 0
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: sheet.visibleRows.length + (sheet.visibleRows.length === 1 ? " result" : " results")
                          + (sheet.loading ? "   ·   still searching…" : "")
                    color: theme.ink; font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Pirate Bay · ExtraTorrents · Torrents-CSV"
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 1
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: sheet.visibleRows.length === 0
                text: sheet.loading ? "Searching comic sources…"
                      : (sheet.errors.length > 0
                         ? "Some sources did not answer. Showing the results that arrived."
                         : "No torrents matched this query. Try another title or ISBN.")
                color: sheet.errors.length > 0 ? "#e6a3a3" : theme.inkDim
                font.family: theme.ui; font.pixelSize: 16
            }

            ListView {
                id: list
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: tableHead.bottom; anchors.bottom: parent.bottom
                anchors.topMargin: 4; anchors.bottomMargin: 8
                clip: true
                visible: sheet.visibleRows.length > 0
                model: sheet.visibleRows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: HouseScrollBar { flick: list }

                delegate: Item {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    height: 150

                    Rectangle {
                        anchors.fill: parent
                        color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                    }

                    Rectangle {
                        id: srcBadge
                        anchors.left: parent.left; anchors.leftMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        width: 54; height: 54; radius: 12
                        color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                        Text {
                            anchors.centerIn: parent
                            text: String(row.modelData.sourceName || "?").charAt(0).toUpperCase()
                            color: theme.ink; font.family: theme.display; font.pixelSize: 24; font.weight: Font.DemiBold
                        }
                    }

                    Column {
                        anchors.left: srcBadge.right; anchors.leftMargin: 24
                        anchors.right: pickBtn.left; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7

                        Row {
                            spacing: 12
                            Text {
                                text: row.modelData.sourceName || "Torrent"
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: sheet.confidenceLabel(row.modelData.confidence)
                                color: sheet.confidenceColor(row.modelData.confidence)
                                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.5
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width: parent.width
                            text: row.modelData.title || ""
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 14; elide: Text.ElideRight
                        }
                        // Restrained evidence: a FORMAT RANGE / coverage badge, an ISSUES
                        // badge, and an uploader-trust marker — the SAME gold evidence
                        // styling as the confidence label above, no new color/emoji.
                        Row {
                            spacing: 7
                            Rectangle {
                                visible: !!row.modelData.coverage
                                width: covText.implicitWidth + 16; height: 20; radius: 6
                                color: Qt.rgba(0.94, 0.77, 0.29, 0.12); border.width: 1
                                border.color: Qt.rgba(0.94, 0.77, 0.29, 0.4)
                                Text {
                                    id: covText; anchors.centerIn: parent; text: "FORMAT RANGE"
                                    color: theme.gold; font.family: theme.ui; font.pixelSize: 10
                                    font.weight: Font.DemiBold; font.letterSpacing: 0.6
                                }
                            }
                            Rectangle {
                                visible: (row.modelData.evidence || []).indexOf("ISSUES") >= 0
                                width: issText.implicitWidth + 16; height: 20; radius: 6
                                color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                                Text {
                                    id: issText; anchors.centerIn: parent; text: "ISSUES"
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 10
                                    font.weight: Font.DemiBold; font.letterSpacing: 0.6
                                }
                            }
                            Rectangle {
                                visible: !!row.modelData.uploader && Number(row.modelData.trustTier) <= 2
                                width: upText.implicitWidth + 16; height: 20; radius: 6
                                color: Qt.rgba(0.94, 0.77, 0.29, 0.12); border.width: 1
                                border.color: Qt.rgba(0.94, 0.77, 0.29, 0.4)
                                Text {
                                    id: upText; anchors.centerIn: parent
                                    text: "TRUSTED · " + String(row.modelData.uploader || "")
                                    color: theme.gold; font.family: theme.ui; font.pixelSize: 10
                                    font.weight: Font.DemiBold; font.letterSpacing: 0.6
                                }
                            }
                        }
                        Text {
                            text: {
                                var p = []
                                if (row.modelData.sizeText) p.push(String(row.modelData.sizeText))
                                if (row.modelData.seeders !== undefined) p.push("\u{1F464} " + row.modelData.seeders)
                                return p.join("   ·   ")
                            }
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        id: pickBtn
                        anchors.right: parent.right; anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        width: 56; height: 56; radius: 28; color: theme.gold
                        scale: rowMa.containsMouse ? 1.05 : 1.0
                        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        Text {
                            anchors.centerIn: parent; text: "↓"; color: "#1a1306"
                            font.pixelSize: 18; font.weight: Font.DemiBold
                        }
                    }
                    MouseArea {
                        id: rowMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sheet.selectRow(row.modelData)
                    }
                }
            }
            ScrollGlide { flick: list }
        }

        // ---- inspecting: the automatic pack path is resolving metadata ----
        Item {
            anchors.fill: parent
            visible: sheet.selectionState === "inspecting"
            Column {
                anchors.centerIn: parent
                width: parent.width - 80
                spacing: 14
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: "Inspecting pack…"
                    color: theme.ink; font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    visible: !!(sheet.pendingRow && sheet.pendingRow.title)
                    text: sheet.pendingRow ? String(sheet.pendingRow.title || "") : ""
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                }
            }
        }

        // ---- archive picker (ambiguous manifest — pack candidates {index,path,bytes}) ----
        ComicTorrentArchivePicker {
            anchors.fill: parent
            visible: sheet.selectionState === "ambiguous"
            files: sheet.archiveFiles
            onArchiveChosen: (fileIndex) => sheet.chooseArchive(fileIndex)
        }

        // ---- incomplete: names the missing issues, never auto-downloads ----
        Item {
            anchors.fill: parent
            visible: sheet.selectionState === "incomplete"
            Column {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 40; anchors.rightMargin: 40
                anchors.verticalCenter: parent.verticalCenter
                spacing: 18
                Text {
                    width: parent.width
                    text: "This pack is missing issues this edition needs."
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 17; wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: (sheet.missingIssues || []).join("   ·   ")
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 14; wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 14
                    Rectangle {
                        width: 190; height: 44; radius: 12
                        color: anotherMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1; border.color: theme.edge
                        Text { anchors.centerIn: parent; text: "Try another source"; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 14 }
                        MouseArea { id: anotherMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: sheet.rejectIncomplete(false) }
                    }
                    Rectangle {
                        width: 190; height: 44; radius: 12
                        color: manualMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1; border.color: theme.edge
                        Text { anchors.centerIn: parent; text: "Search manually"; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 14 }
                        MouseArea { id: manualMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: sheet.rejectIncomplete(true) }
                    }
                }
            }
        }

        // ---- combined: only a whole multi-edition archive covers this edition ----
        Item {
            anchors.fill: parent
            visible: sheet.selectionState === "combined"
            Column {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 40; anchors.rightMargin: 40
                anchors.verticalCenter: parent.verticalCenter
                spacing: 18
                Text {
                    width: parent.width
                    text: "Only a combined archive covers this edition — it likely includes other editions too."
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 17; wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 14
                    Rectangle {
                        width: 150; height: 44; radius: 12
                        color: combBackMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1; border.color: theme.edge
                        Text { anchors.centerIn: parent; text: "Go back"; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15 }
                        MouseArea { id: combBackMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: sheet.rejectCombined() }
                    }
                    Rectangle {
                        width: 260; height: 44; radius: 12
                        color: combGoMa.containsMouse ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.85)
                        Behavior on color { ColorAnimation { duration: 140 } }
                        Text { anchors.centerIn: parent; text: "Download whole archive anyway"; color: "#1a1306"
                            font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                        MouseArea { id: combGoMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: sheet.confirmCombined() }
                    }
                }
            }
        }
    }

    // ── weak-match confirmation ──
    Item {
        anchors.fill: parent
        visible: sheet.confirmingWeak
        z: 40
        MouseArea { anchors.fill: parent }   // swallow clicks behind the prompt
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 2 * theme.margin)
            height: promptCol.implicitHeight + 56
            radius: 18
            color: Qt.rgba(0.08, 0.08, 0.09, 0.98)
            border.width: 1; border.color: theme.edge
            Column {
                id: promptCol
                anchors.left: parent.left; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 28; anchors.rightMargin: 28
                spacing: 18
                Text {
                    width: parent.width
                    text: "This release does not closely match the collected edition."
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 17; wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    visible: !!(sheet.pendingRow && sheet.pendingRow.title)
                    text: sheet.pendingRow ? String(sheet.pendingRow.title || "") : ""
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap
                }
                Row {
                    anchors.right: parent.right
                    spacing: 14
                    Rectangle {
                        width: 120; height: 44; radius: 12
                        color: backMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1; border.color: theme.edge
                        Text { anchors.centerIn: parent; text: "Go back"; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15 }
                        MouseArea { id: backMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: sheet.cancelWeakSelection() }
                    }
                    Rectangle {
                        width: 150; height: 44; radius: 12; color: caMa.containsMouse ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.85)
                        Behavior on color { ColorAnimation { duration: 140 } }
                        Text { anchors.centerIn: parent; text: "Choose anyway"; color: "#1a1306"
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                        MouseArea { id: caMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: sheet.confirmWeakSelection() }
                    }
                }
            }
        }
    }
}
