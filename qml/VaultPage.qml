// VaultPage — "On this machine": the local-media Vault as a host-owned full page, entered from
// the taskbar folder door. Slice 10 lands the permanent door + this page's EMPTY state (nothing
// indexed yet): eyebrow, title, and a dashed Add-folder drop surface. It paints from the
// VaultLibrary read-model (itemCount/scanning); the shelves that fill a populated Vault, and the
// folder-scan ingest behind Add folder, land in Slice 11. Same chrome vocabulary as
// Settings/Downloads (back · minimize · fullscreen · power) so it reads as one of the house's pages.
import QtQuick
import QtQuick.Controls
import "VaultApi.js" as VaultApi
import "TheatreApi.js" as TheatreApi

Item {
    id: root
    objectName: "vaultPage"
    property Item backdrop: null
    signal backRequested()
    signal addFolderRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    // Slice 14: a folder-view row / preview door asked to open a file. Carries only the path —
    // LocalLaunch (C++) re-derives family + vault id + title, so routing/identity has one owner
    // (the same path the picker, drag-drop, and Open Recent funnel through win.openLocalMedia).
    signal openMediaRequested(string path)
    signal viewWorldRequested(var identity)

    Theme { id: theme }

    // ---- read-model: the Vault's published truth (revision-driven refresh) ----
    // Touch revision so every shelf/count re-reads on a committed publish; itemCount/scanning drive
    // the empty vs scanning-empty state (a populated Vault + its shelves arrive in Slice 11).
    readonly property int itemCount:
        (typeof VaultLibrary !== "undefined") ? (VaultLibrary.revision, VaultLibrary.itemCount) : 0
    readonly property bool scanning:
        (typeof VaultLibrary !== "undefined") ? VaultLibrary.scanning : false
    readonly property bool scanningEmpty: itemCount === 0 && scanning
    // Keep existing shelves instantiated during a background rescan. A returned drive remains
    // visibly unavailable until the successful publish replaces its away rows with fresh facts.
    readonly property bool populated: itemCount > 0
    // Lanista/plan state contract (vaultPage.vaultState / itemCount / cardVisible).
    readonly property string vaultState: scanning ? "scanning" : (itemCount > 0 ? "populated" : "empty")
    readonly property bool cardVisible: (typeof VaultLibrary !== "undefined") ? VaultLibrary.cardVisible : false
    // Read-only { id -> admissionVerdict } for video rows, re-read on the same revision clock. The
    // Continue rail gates on this so only durably-Admitted local videos ever resume.
    readonly property var admissionById:
        (typeof VaultLibrary !== "undefined")
            ? (VaultLibrary.revision, VaultLibrary.admissionById())
            : ({})

    // ---- Slice 12 dress: the in-world tab bar (All · Comics · Books · Video · Folders) ----
    property string currentTab: "all"
    readonly property var tabModel: [
        { key: "all", label: "All" }, { key: "comic", label: "Comics" },
        { key: "book", label: "Books" }, { key: "video", label: "Video" },
        { key: "folders", label: "Folders" }, { key: "hidden", label: "Hidden" }
    ]
    property var autoFilmEnrichmentRequested: ({})
    function requestAutoFilmEnrichment(list) {
        if (!list) return list
        for (var i = 0; i < list.length; i++) {
            var tile = list[i]
            if (!tile || tile.identSource !== "IMDB" || !tile.identityId) continue
            var key = String(tile.key || tile.identityId)
            if (root.autoFilmEnrichmentRequested[key]) continue
            root.autoFilmEnrichmentRequested[key] = true
            root.requestProgressiveFilmIdentity(tile)
        }
        return list
    }
    function seriesFor(kind) {
        var list = (typeof VaultLibrary !== "undefined")
            ? (VaultLibrary.revision, VaultLibrary.series(kind)) : []
        return kind === "video" ? root.requestAutoFilmEnrichment(list) : list
    }
    // Kinds whose shelf shows under the current tab; Folders is a flat all-kinds gallery instead.
    function shelfKinds() {
        if (root.currentTab === "comic" || root.currentTab === "book" || root.currentTab === "video")
            return [root.currentTab]
        if (root.currentTab === "folders") return []
        if (root.currentTab === "hidden") return []
        return ["comic", "book", "video"]
    }
    function allSeries() {
        return root.seriesFor("comic").concat(root.seriesFor("book")).concat(root.seriesFor("video"))
    }
    function hiddenSeries() {
        return (typeof VaultLibrary !== "undefined") ? (VaultLibrary.revision, VaultLibrary.hiddenSeries()) : []
    }
    function revealTile(data) {
        if (typeof VaultLibrary !== "undefined" && data && data.subtreePath)
            VaultLibrary.revealInExplorer(data.subtreePath)
    }
    function identifyTile(data) {
        if (!data) return
        identifyDialog.groupKey = data.key || ""
        identifyDialog.titleText = data.title || ""
        identifyDialog.kind = data.kind || ""
        identifyDialog.embeddedIdentity = ({})
        if (identifyDialog.kind === "book" && typeof VaultLibrary !== "undefined") {
            var bookRows = VaultLibrary.items("book", identifyDialog.groupKey) || []
            var book = bookRows.length ? bookRows[0] : {}
            if (book.title || book.displayTitle) {
                identifyDialog.embeddedIdentity = {
                    title: book.title || book.displayTitle || data.title,
                    sourceId: "epub:" + String(book.id || identifyDialog.groupKey),
                    synopsis: book.synopsis || "",
                    coverUrl: book.coverUrl || "",
                    year: Number(book.year || 0)
                }
            }
        }
        identifyDialog.feedback = ""
        identifyDialog.open()
    }
    function requestProgressiveFilmIdentity(tile) {
        if (!tile || tile.identSource !== "IMDB" || !tile.identityId) return
        var imdbId = String(tile.identityId).replace(/^imdb:/, "")
        function applyMeta(meta) {
            if (!meta) return
            var synopsis = String(meta.description || meta.overview || meta.plot || "")
            var poster = TheatreApi.normalizeArtUrl(meta.poster || meta.cover || "")
            if (typeof VaultLibrary !== "undefined")
                VaultLibrary.enrichIdentity(tile.key || "", synopsis, poster)
            var facts = root.folderDetailFacts || ({})
            facts.synopsis = synopsis
            facts.synopsisSource = synopsis.length ? "Cinemeta" : (facts.synopsisSource || "IMDB")
            facts.coverUrl = poster || facts.coverUrl || ""
            root.folderDetailFacts = facts
            if (folderLayer.item) {
                folderLayer.item.synopsis = facts.synopsis || ""
                folderLayer.item.synopsisSource = facts.synopsisSource || ""
                if (poster) folderLayer.item.coverUrl = poster
            }
        }
        TheatreApi.loadMeta("movie", imdbId, function(meta) {
            if (meta) applyMeta(meta)
            else TheatreApi.loadMeta("series", imdbId, applyMeta)
        })
    }

    // ---- Slice 14: the Vault Continue rail — the app's own local reads/watches, resumable. Live
    //      from Progress.recent filtered to vault: ids (catalogue recents keep their own rails, §9).
    //      Re-derives on Progress.revision (a lifecycle write), never the silent 5s video tick. ----
    readonly property var continueItems: (root.populated && typeof Progress !== "undefined")
        ? VaultApi.continueRail(Progress, (Progress.revision, 18), root.admissionById)
        : []

    // ---- Slice 13: the folder detail overlay. Vault-local — the shelves stay instantiated
    //      underneath (hidden), so their scroll position survives open → Back for free. A row
    //      snapshot is seeded on open (not re-queried while a background scan runs). ----
    property bool folderDetailOpen: false
    property var folderDetailFacts: ({})
    // The static index snapshot (files as they sit on disk); seeded on open, NOT re-queried while a
    // background scan runs — the S13 snapshot discipline. The live read-state join happens below.
    property var folderDetailBaseRows: []
    // Rows the folder view actually renders: the index snapshot joined against live Progress so the
    // read tick, gold hairline, and last-read sort reflect real reads. Re-joins on Progress.revision
    // — a lifecycle write (open/close/minimize) — so a comic read then Back updates the tick; it does
    // NOT re-join on the silent 5s video tick (recordSilent bumps no revision), so the join can never
    // reintroduce the Continue-repaint stutter cascade (Preflight's reactivity hazard).
    readonly property var folderDetailRows: (root.folderDetailOpen && typeof Progress !== "undefined")
        ? VaultApi.joinRows(Progress, (Progress.revision, root.folderDetailBaseRows))
        : root.folderDetailBaseRows
    function openFolder(tile) {
        if (!tile) return
        root.folderDetailFacts = tile
        root.folderDetailBaseRows = (typeof VaultLibrary !== "undefined")
            ? VaultLibrary.items(tile.kind, tile.key) : []
        root.folderDetailOpen = true
        root.requestProgressiveFilmIdentity(tile)
    }
    function closeFolder() { root.folderDetailOpen = false }
    // Push the re-joined rows into the live folder view when Progress changes under it (e.g. after a
    // read while the folder view sits occluded beneath the reader). onLoaded seeds the first model.
    onFolderDetailRowsChanged: if (folderLayer.item) folderLayer.item.model = root.folderDetailRows

    // Shared shelf tile: a real comic cover when the row carries one (image://comiccover), else the
    // honest kind-icon on a gradient (book/video art is a later slice). Reused by every shelf + Folders.
    Component {
        id: vaultTileLegacyComp
        Column {
            id: tile
            required property var modelData
            objectName: "vaultTile_" + (modelData.key || "")
            readonly property bool away: Number(modelData.awayCount || 0) > 0
            readonly property bool hasErrors: Number(modelData.errorCount || 0) > 0
            spacing: 8
            Rectangle {
                id: coverBox
                width: 150; height: 208; radius: 12; clip: true
                border.width: 1; border.color: theme.edge
                opacity: tile.away ? 0.48 : 1.0
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0.16, 0.14, 0.20, 1) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.055, 0.060, 0.090, 1) }
                }
                Image { // real cover art (comics after enrichment) — filling the whole tile
                    anchors.fill: parent
                    visible: !!modelData.coverUrl
                    source: modelData.coverUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: true
                }
                Image { // honest kind icon when there is no cover yet (book/video, un-enriched comics)
                    anchors.centerIn: parent; width: 34; height: 34; opacity: 0.4
                    visible: !modelData.coverUrl
                    source: modelData.kind === "book" ? "../assets/icons/book-library.svg"
                          : modelData.kind === "video" ? "../assets/icons/projector-theatre.svg"
                          : "../assets/icons/comic-book.svg"
                    fillMode: Image.PreserveAspectFit
                }
                Rectangle {
                    anchors.fill: parent
                    visible: tile.away || tile.hasErrors
                    color: Qt.rgba(0.04, 0.04, 0.04, tile.away ? 0.54 : 0.38)
                    Text {
                        anchors.centerIn: parent
                        text: tile.away ? "Unavailable" : "Needs attention"
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                    }
                }
                // kind badge, top-left
                Rectangle {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
                    radius: 99; height: 20; width: badgeT.implicitWidth + 16
                    color: Qt.rgba(0, 0, 0, 0.62); border.width: 1; border.color: theme.edge
                    Text {
                        id: badgeT; anchors.centerIn: parent
                        text: modelData.kind === "comic" ? "COMIC" : modelData.kind === "book" ? "BOOK" : "VIDEO"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.6
                    }
                }
                // scrim so the overlaid title reads over any art
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 76
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.82) }
                    }
                }
                // title, overlaid at the foot of the cover
                Text {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: 9; anchors.rightMargin: 9; anchors.bottomMargin: 9
                    text: modelData.title || ""
                    color: "#f2f2f0"; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.9)
                }
                MouseArea {   // open the folder detail (Slice 13)
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !tile.away
                    onClicked: root.openFolder(modelData)
                }
            }
            Text {
                text: (modelData.count || 0) + ((modelData.count === 1) ? " item" : " items")
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.4
            }
        }
    }

    // The extracted tile is the production delegate; the legacy component above remains inert as
    // a short-lived source reference while the shelf transition is review-gated.
    Component {
        id: vaultTileComp
        VaultTile {
            onFolderRequested: (data) => root.openFolder(data)
            onOpenRequested: (data) => root.openFolder(data)
            onRevealRequested: (data) => root.revealTile(data)
            onIdentifyRequested: (data) => root.identifyTile(data)
            onUnidentifyRequested: (data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.unidentifyGroup(data.key || "")
            }
            onReshelveRequested: (kind, data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.reshelveGroup(data.key || "", kind)
            }
            onHideRequested: (data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.hideGroup(data.key || "")
            }
            onRestoreRequested: (data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.restoreGroup(data.key || "")
            }
        }
    }

    // A Vault Continue tile: cover (or honest kind-icon on a gradient), title, a gold resume
    // hairline, and a click that reopens through the shared LocalLaunch path (openMediaRequested).
    // Shape from VaultApi.continueRail: { id, kind, path, title, cover, progressFraction }.
    Component {
        id: vaultContinueTileComp
        Column {
            required property var modelData
            spacing: 8
            Rectangle {
                width: 150; height: 208; radius: 12; clip: true
                border.width: 1; border.color: theme.edge
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0.16, 0.14, 0.20, 1) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.055, 0.060, 0.090, 1) }
                }
                Image {
                    anchors.fill: parent
                    visible: !!modelData.cover
                    source: modelData.cover || ""
                    fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                }
                Image {
                    anchors.centerIn: parent; width: 34; height: 34; opacity: 0.4
                    visible: !modelData.cover
                    source: modelData.kind === "book" ? "../assets/icons/book-library.svg"
                          : modelData.kind === "video" ? "../assets/icons/projector-theatre.svg"
                          : "../assets/icons/comic-book.svg"
                    fillMode: Image.PreserveAspectFit
                }
                Rectangle {   // kind badge, top-left
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
                    radius: 99; height: 20; width: contBadgeT.implicitWidth + 16
                    color: Qt.rgba(0, 0, 0, 0.62); border.width: 1; border.color: theme.edge
                    Text {
                        id: contBadgeT; anchors.centerIn: parent
                        text: modelData.kind === "comic" ? "COMIC" : modelData.kind === "book" ? "BOOK" : "VIDEO"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.6
                    }
                }
                Rectangle {   // scrim behind the title
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 76
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.82) }
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: 9; anchors.rightMargin: 9; anchors.bottomMargin: 12
                    text: modelData.title || ""
                    color: "#f2f2f0"; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.9)
                }
                Rectangle {   // gold resume hairline — the real read/watch position
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 3; color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: parent.width * Math.max(0, Math.min(1, modelData.progressFraction || 0))
                        color: theme.gold
                    }
                }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: if (modelData.path) root.openMediaRequested(modelData.path)
                }
            }
            Text {
                text: modelData.kind === "comic" ? "Comic" : modelData.kind === "book" ? "Book" : "Video"
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.4
            }
        }
    }

    // On open (the vaultLayer Loader recreates this page each time), resume the founding card
    // for a folder added-but-never-confirmed. C++ dedups to once per app run (Slice 11 Thread D).
    Component.onCompleted: {
        if (typeof VaultLibrary !== "undefined") {
            VaultLibrary.offerUnconfirmedRoots()
            VaultLibrary.rescanDegradedRoots()   // Slice 15: watcher-failure fallback, silently
        }
    }

    // swallow clicks so nothing behind this page receives them
    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- live shell wallpaper (the same backdrop sampling the other full pages use) ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.86) }
    }

    Flickable {
        id: page
        // Kept instantiated while the folder detail is open (only hidden), so contentY survives.
        visible: !root.folderDetailOpen
        enabled: !root.folderDetailOpen
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 150
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header (empty/scanning states only — populated leads with the marquee panel) ----
            Column {
                visible: !root.populated
                width: col.width
                spacing: 0
                Text { text: "ON THIS MACHINE"; color: theme.inkDimmer
                       font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
                Text { text: "Vault"; color: theme.ink; topPadding: 8
                       font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
                Item { width: 1; height: 20 }
                Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }
            }

            // ---- populated: the world-treatment marquee (mock C) — a gradient hero panel with the
            //      eyebrow, identity, honest counts, and the founding CTA, entered via the taskbar
            //      door + ‹ Back above. This is the world dress fused onto the sub-app door. ----
            Item { visible: root.populated; width: 1; height: 6 }
            Rectangle {
                id: marquee
                objectName: "vaultMarquee"
                visible: root.populated
                width: col.width
                height: root.populated ? marqueeCol.implicitHeight + 84 : 0
                radius: 18
                border.width: 1; border.color: theme.edge
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0.094, 0.106, 0.133, 1) }
                    GradientStop { position: 0.6; color: Qt.rgba(0.063, 0.075, 0.102, 1) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.047, 0.055, 0.075, 1) }
                }
                // Lanista/plan marquee contract.
                property int itemCount: root.itemCount
                property int folderCount: (typeof VaultLibrary !== "undefined")
                                          ? (VaultLibrary.revision, VaultLibrary.rootCount()) : 0
                property int kindCount: (root.seriesFor("comic").length > 0 ? 1 : 0)
                                      + (root.seriesFor("book").length > 0 ? 1 : 0)
                                      + (root.seriesFor("video").length > 0 ? 1 : 0)

                // Soft gold warmth in the top-right (approximates the mock's radial glow).
                Rectangle {
                    anchors.right: parent.right; anchors.top: parent.top
                    width: parent.width * 0.62; height: parent.height * 0.9
                    radius: 18
                    opacity: 0.12
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: theme.gold }
                    }
                }

                Column {
                    id: marqueeCol
                    x: 44; y: 42
                    width: parent.width - 88
                    spacing: 0

                    Text { text: "ON THIS MACHINE"; color: theme.gold
                           font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3.5; font.weight: Font.DemiBold }
                    Text { topPadding: 10; text: "Vault"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 52; font.letterSpacing: -0.5 }

                    Row {
                        topPadding: 24
                        spacing: 40
                        Column { spacing: 2
                            Text { text: marquee.itemCount; color: theme.ink; font.family: theme.display; font.pixelSize: 30 }
                            Text { text: "ITEMS"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.2 }
                        }
                        Column { spacing: 2
                            Text { text: marquee.folderCount; color: theme.ink; font.family: theme.display; font.pixelSize: 30 }
                            Text { text: marquee.folderCount === 1 ? "FOLDER" : "FOLDERS"; color: theme.inkDimmer
                                   font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.2 }
                        }
                        Column { spacing: 2
                            Text { text: marquee.kindCount; color: theme.ink; font.family: theme.display; font.pixelSize: 30 }
                            Text { text: marquee.kindCount === 1 ? "KIND" : "KINDS"; color: theme.inkDimmer
                                   font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.2 }
                        }
                    }

                    // Founding CTA — Add folder (gold), with the live scan pill beside it while scanning.
                    Row {
                        topPadding: 26
                        spacing: 14
                        Rectangle {
                            objectName: "vaultMarqueeAddFolder"
                            width: addMarqueeT.implicitWidth + 48; height: 46; radius: 12
                            color: addMarqueeMa.containsMouse ? Qt.rgba(0.98, 0.82, 0.36, 1) : theme.gold
                            Text { id: addMarqueeT; anchors.centerIn: parent; text: "Add folder"
                                   color: "#151310"; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                            MouseArea { id: addMarqueeMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor; onClicked: root.addFolderRequested() }
                        }
                    }
                }
            }

            Item { visible: root.populated; width: 1; height: 30 }

            // ---- Vault Continue rail: local reads/watches in progress, resumable in one click.
            //      On the All (home) tab only, above the shelves (marquee → Continue → shelves). ----
            Column {
                id: continueSection
                visible: root.populated && root.currentTab === "all" && root.continueItems.length > 0
                width: col.width
                spacing: 14
                bottomPadding: 30
                Item {
                    width: col.width
                    height: continueHdr.implicitHeight
                    Text {
                        id: continueHdr
                        anchors.left: parent.left; anchors.bottom: parent.bottom
                        text: "Continue"; color: theme.ink; font.family: theme.display; font.pixelSize: 28
                    }
                }
                ListView {
                    objectName: "vaultShelf_continue"
                    property int rowCount: root.continueItems.length   // Lanista contract
                    width: col.width
                    height: 250
                    orientation: ListView.Horizontal
                    spacing: 16
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: (root.populated && root.currentTab === "all") ? root.continueItems : []
                    delegate: vaultContinueTileComp
                }
            }

            // Per-kind shelves — shown for the All tab or the matching kind tab.
            Repeater {
                model: root.populated ? root.shelfKinds() : []
                delegate: Column {
                    id: kindSection
                    required property string modelData
                    property string shelfSuffix: modelData === "comic" ? "comics"
                                               : modelData === "book" ? "books" : "video"
                    property var seriesList: root.seriesFor(modelData)
                    visible: seriesList.length > 0
                    width: col.width
                    spacing: 14
                    bottomPadding: 30

                    Item {
                        width: col.width
                        height: hdrTitle.implicitHeight
                        Text {
                            id: hdrTitle
                            anchors.left: parent.left; anchors.bottom: parent.bottom
                            text: kindSection.modelData === "comic" ? "Comics"
                                : kindSection.modelData === "book" ? "Books" : "Video"
                            color: theme.ink; font.family: theme.display; font.pixelSize: 28
                        }
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                            text: kindSection.seriesList.length
                                  + (kindSection.modelData === "video" ? " titles" : " series")
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                    Flow {
                        objectName: "vaultShelf_" + kindSection.shelfSuffix
                        property int rowCount: kindSection.seriesList.length // Lanista contract
                        property int awayCount: {
                            var total = 0
                            for (var i = 0; i < kindSection.seriesList.length; ++i)
                                total += Number(kindSection.seriesList[i].awayCount || 0)
                            return total
                        }
                        property int errorCount: {
                            var total = 0
                            for (var i = 0; i < kindSection.seriesList.length; ++i)
                                total += Number(kindSection.seriesList[i].errorCount || 0)
                            return total
                        }
                        width: col.width
                        spacing: 16
                        Repeater { model: kindSection.seriesList; delegate: vaultTileComp }
                    }
                }
            }

            // Folders tab — every series across kinds as one gallery.
            Column {
                id: foldersSection
                visible: root.populated && root.currentTab === "folders"
                width: col.width
                spacing: 14
                bottomPadding: 30
                property var allList: (root.populated && root.currentTab === "folders") ? root.allSeries() : []
                Item {
                    width: col.width
                    height: foldersTitle.implicitHeight
                    Text {
                        id: foldersTitle
                        anchors.left: parent.left; anchors.bottom: parent.bottom
                        text: "Folders"; color: theme.ink; font.family: theme.display; font.pixelSize: 28
                    }
                    Text {
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                        text: foldersSection.allList.length + " folders"
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                    }
                }
                Flow {
                    objectName: "vaultShelf_folders"
                    property int rowCount: foldersSection.allList.length
                    property int awayCount: {
                        var total = 0
                        for (var i = 0; i < foldersSection.allList.length; ++i)
                            total += Number(foldersSection.allList[i].awayCount || 0)
                        return total
                    }
                    width: col.width
                    spacing: 16
                    Repeater { model: foldersSection.allList; delegate: vaultTileComp }
                }
            }

            // Hidden items are a reversible shelf, not a second filesystem state.
            Column {
                id: hiddenSection
                visible: root.populated && root.currentTab === "hidden"
                width: col.width
                spacing: 14
                bottomPadding: 30
                property var hiddenList: (root.populated && root.currentTab === "hidden") ? root.hiddenSeries() : []
                Item {
                    width: col.width
                    height: hiddenTitle.implicitHeight
                    Text {
                        id: hiddenTitle
                        anchors.left: parent.left; anchors.bottom: parent.bottom
                        text: "Hidden"; color: theme.ink; font.family: theme.display; font.pixelSize: 28
                    }
                    Text {
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                        text: hiddenSection.hiddenList.length + " folders"
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                    }
                }
                Flow {
                    objectName: "vaultShelf_hidden"
                    property int rowCount: hiddenSection.hiddenList.length
                    width: col.width
                    spacing: 16
                    Repeater { model: hiddenSection.hiddenList; delegate: vaultTileComp }
                }
                Text {
                    visible: hiddenSection.hiddenList.length === 0
                    text: "Nothing is hidden."
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                }
            }

            // Bottom clearance so the last shelf clears the fixed in-world tab bar. (Add folder now
            // lives in the marquee CTA.)
            Item { visible: root.populated; width: 1; height: 84 }

            // ---- empty state: the dashed Add-folder drop surface (shown until the Vault has content) ----
            Item { visible: !root.populated; width: 1; height: 44 }

            Rectangle {
                id: dropSurface
                visible: !root.populated
                objectName: "vaultDropSurface"
                width: col.width
                height: 320
                radius: 20
                color: dropHover.containsDrag ? Qt.rgba(0.94, 0.77, 0.29, 0.06)
                                              : Qt.rgba(0.04, 0.045, 0.065, 0.42)

                // QML has no dashed Rectangle border, so paint one — brightens to gold on drag-over.
                Canvas {
                    id: dashes
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        ctx.strokeStyle = dropHover.containsDrag ? "rgba(240,196,74,0.85)" : "rgba(255,255,255,0.22)"
                        ctx.lineWidth = 1.6
                        ctx.setLineDash([9, 7])
                        ctx.strokeRect(1, 1, width - 2, height - 2)
                    }
                }
                Connections { target: dropHover; function onContainsDragChanged() { dashes.requestPaint() } }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 96
                    spacing: 16

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 48; height: 48; opacity: 0.7
                        source: "../assets/icons/vault-folder.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: root.scanningEmpty ? "Looking through your folder…"
                                                 : "Add a folder of comics, books, or videos"
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: 18; font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: !root.scanningEmpty
                        text: "The Vault reads what is already on this machine and keeps it here — nothing is downloaded or moved."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13; lineHeight: 1.3
                    }

                    // Add folder — opens the native folder picker (Slice 10). The ingest behind it
                    // (canonicalize · add as a Vault root · scan · shelve) lands in Slice 11.
                    Rectangle {
                        objectName: "vaultAddFolderButton"
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !root.scanningEmpty
                        width: addLabel.implicitWidth + 44; height: 44; radius: 12
                        color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.9) : Qt.rgba(1, 1, 1, 0.08)
                        border.width: 1
                        border.color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.6) : theme.edge
                        Text {
                            id: addLabel
                            anchors.centerIn: parent
                            text: "Add folder"
                            color: addMa.containsMouse ? "#141207" : theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: addMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.addFolderRequested()
                        }
                    }
                }

                // A folder dropped on THIS Vault-specific surface is an Add-folder gesture (Slice 10
                // opens the picker path; ingest is Slice 11). This is NOT the app-wide file-open drop.
                DropArea {
                    id: dropHover
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (drop) => {
                        drop.accepted = true
                        root.addFolderRequested()
                    }
                }
            }
        }
    }

    // ---- scan pill (Slice 11): a folder census is running; cancelable. Shows the folder name;
    //      the live "N of M" count fills in once the scanner emits per-file progress. ----
    Rectangle {
        id: scanPill
        objectName: "vaultScanPill"
        property bool scanning: (typeof VaultLibrary !== "undefined") ? VaultLibrary.scanning : false
        property int doneCount: (typeof VaultLibrary !== "undefined")
                                ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanDone) : 0
        property int totalCount: (typeof VaultLibrary !== "undefined")
                                 ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanTotal) : 0
        property string rootPath: (typeof VaultLibrary !== "undefined")
                                  ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanningRoot) : ""
        visible: scanning && !root.folderDetailOpen
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        width: pillRow.implicitWidth + 40
        height: 52
        radius: 26
        color: Qt.rgba(0.04, 0.045, 0.065, 0.94)
        border.width: 1
        border.color: theme.edge

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 14

            Item {
                width: 16; height: 16
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    id: spinner
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        width: 4; height: 4; radius: 2; color: theme.gold
                        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                    }
                    RotationAnimator on rotation {
                        running: scanPill.scanning; from: 0; to: 360
                        duration: 900; loops: Animation.Infinite
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    var name = scanPill.rootPath.split(/[\\/]/).pop()
                    var base = "Scanning " + (name || "folder")
                    return (scanPill.totalCount > 0)
                        ? (base + " — " + scanPill.doneCount + " of " + scanPill.totalCount)
                        : (base + "…")
                }
                color: theme.ink
                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
            }

            Rectangle {
                objectName: "vaultScanCancel"
                width: cancelLabel.implicitWidth + 22; height: 30; radius: 15
                anchors.verticalCenter: parent.verticalCenter
                color: cancelMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.07)
                Text {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: "Cancel"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 13
                }
                MouseArea {
                    id: cancelMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (typeof VaultLibrary !== "undefined") VaultLibrary.cancelScan()
                }
            }
        }
    }

    // ---- in-world tab bar (Slice 12): All · Comics · Books · Video · Folders. Uses WorldTabBar
    //      as-committed (no tabPrefix yet — the shared file carries another lane's WIP), so its pills
    //      aren't Lanista-addressable until that lands; tab logic is covered by tst_vault_home + eyes. ----
    WorldTabBar {
        visible: root.populated && !root.folderDetailOpen
        backdrop: root.backdrop
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 30
        width: Math.min(560, parent.width - 80)
        tabModel: root.tabModel
        currentTab: root.currentTab
        onTabRequested: (t) => root.currentTab = t
    }

    // ---- top chrome: minimize · fullscreen · power (same vocabulary as Settings/Downloads) ----
    // z above the folder overlay so the window controls stay usable inside the detail view.
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.rightMargin: theme.margin
        z: 60
        width: chromeRow.implicitWidth
        height: 30
        Row {
            id: chromeRow
            spacing: 22
            Text { text: "—"; color: mMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: mMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Text { text: "⛶"; color: fMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: fMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }
            Text { text: "⏻"; color: pMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }
    BackAction {
        variant: "capsule"; tip: "Back"
        visible: !root.folderDetailOpen   // the folder detail owns Back while it is up
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 21
        anchors.leftMargin: theme.margin - 10
        onTriggered: root.backRequested()
    }

    // ── the founding-ceremony confirmation card: a modal over the Vault once a census yields a
    //    candidate. Seedable component; VaultPage wires it to the VaultLibrary façade. ──
    VaultConfirmCard {
        objectName: "vaultCard"
        anchors.fill: parent
        z: 30
        visible: ((typeof VaultLibrary !== "undefined") ? VaultLibrary.cardVisible : false) && !root.folderDetailOpen
        model: (typeof VaultLibrary !== "undefined") ? (VaultLibrary.candidateChanged, VaultLibrary.candidate) : []
        rootPath: (typeof VaultLibrary !== "undefined") ? VaultLibrary.candidateRoot : ""
        onShelveRequested: (ov) => { if (typeof VaultLibrary !== "undefined") VaultLibrary.confirmRoot(rootPath, ov) }
        onDismissRequested: { if (typeof VaultLibrary !== "undefined") VaultLibrary.dismissCard() }
    }

    // ── Slice 13: the folder detail overlay (z above the shelves + card, below the window chrome).
    //    The shelves stay instantiated (hidden) underneath so Back returns to the same scroll spot. ──
    Loader {
        id: folderLayer
        anchors.fill: parent
        z: 40
        active: root.folderDetailOpen
        source: "VaultFolderView.qml"
        onLoaded: {
            item.backdrop = root.backdrop
            item.title = root.folderDetailFacts.title || ""
            item.kind = root.folderDetailFacts.kind || "comic"
            item.coverUrl = root.folderDetailFacts.coverUrl || ""
            item.rootPath = root.folderDetailFacts.subtreePath || ""
            item.identityId = root.folderDetailFacts.identityId || ""
            item.identitySource = root.folderDetailFacts.identSource || ""
            item.identityWorld = root.folderDetailFacts.identityWorld || ""
            item.synopsis = root.folderDetailFacts.synopsis || ""
            item.synopsisSource = root.folderDetailFacts.synopsisSource || ""
            item.model = root.folderDetailRows
            item.viewWorldRequested.connect(function(identity) { root.viewWorldRequested(identity) })
        }
    }
    Connections {
        target: folderLayer.item
        function onBackRequested() { root.closeFolder() }
        function onRevealRequested(path) {
            if (typeof VaultLibrary !== "undefined") VaultLibrary.revealInExplorer(path)
        }
        // Slice 14 (open half): a row click opens that file; the preview "Continue" door opens the
        // first file that already carries progress (the reader resumes itself at the saved page —
        // the Vault-side read tick / hairline / rail join is the seam-map half, not this one).
        function onOpenRequested(row) {
            if (row && row.path) root.openMediaRequested(row.path)
        }
        function onContinueRequested() {
            // Resume the file with the freshest real Progress; fall back to the first row only if
            // nothing carries progress (defensive — the door reads "Continue" only when some does).
            var rows = root.folderDetailRows || []
            var target = VaultApi.resumeTarget(rows)
            if (!target && rows.length) target = rows[0]
            if (target && target.path) root.openMediaRequested(target.path)
        }
    }

    VaultIdentifyDialog {
        id: identifyDialog
        anchors.centerIn: parent
        z: 80
        onIdentityChosen: (groupKey, identity) => {
            if (typeof VaultLibrary === "undefined") return
            if (VaultLibrary.identifyGroupWith(groupKey, identity)) {
                close()
            } else {
                feedback = "That identity could not be applied. The folder stays filename-honest."
            }
        }
    }
}
