// MangaTankobanLibrary — the volume shelf. A series IS its volumes here: EVERY
// canonical volume the service knows renders as a row.
//
// BUILT TO THEATRE'S EPISODE ROW (qml/TheatreSeries.qml ~1502). A tankobon is the
// same kind of object as an episode — a numbered instalment you own, are part-way
// through, or have not fetched yet — so the shelf wears Theatre's anatomy exactly:
// a 70px number rail with a hairline, the artwork, name + meta, an uppercase status,
// and circular actions on the right. Continue is Theatre's "Next up": taller, gold
// rail, gold number.
//
// The ONE adaptation that is not a copy: Theatre's still is LANDSCAPE (172x96) and
// a tankobon is portrait, so Theatre is the wrong yardstick for the artwork — the
// aspect ratios are not comparable. The right reference is the CHAPTER row on this
// same page, which already shows a portrait first-page thumbnail: 100x140 in a 156px
// row. The book matches it exactly, so a volume and a chapter read at the same
// weight. Continue is the emphasised row and steps up to 126x176 in 196.
//
// The signature: OWNERSHIP IS DRAWN AS A SPINE. The rail's hairline thickens into a
// gold rule and the cover picks up a lit left edge once the volume is on disk, so a
// shelf you own reads as a run of book spines. It encodes the one thing this product
// is about — collecting books — rather than decorating the row.
//
// Service seam: in the app `service` is the native `TankobanVolumes` context
// property; the offscreen harness injects a fake exposing the same API. All calls
// resolve through `serviceObject`, falling back to the context property so the
// running app needs no wiring. `progress` is the same seam over ProgressStore.
import QtQuick

Item {
    id: root

    property string seriesId: ""
    // Injection seam: the harness assigns a fake; the app leaves this null and the
    // calls fall through to the native TankobanVolumes context property.
    property var service: null
    readonly property var serviceObject: root.service
        ? root.service
        : ((typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null)
    // Same injection seam over ProgressStore, for the Continue row.
    property var progress: null
    readonly property var progressObject: root.progress
        ? root.progress
        : ((typeof Progress !== "undefined") ? Progress : null)

    // ── VOLUME COVERS ────────────────────────────────────────────────────────
    // A volume's cover is the FIRST PAGE OF ITS FIRST CHAPTER — the same thing a
    // chapter row shows, fetched the same way. Chapter thumbnails are NOT in
    // WeebCentral's chapter-list HTML; the app scrapes each chapter's first page
    // on demand through Downloads.fetchThumb -> thumbReady(chapterId, url). So the
    // page hands us the live chapter list and we ask for one thumb per volume.
    //
    // No cover is fetched from MangaDex or Comick: MangaDex is retired, and Comick
    // serves exactly one cover per SERIES (the latest volume), not one per volume —
    // probed 2026-07-30 across four endpoint shapes.
    property var chapters: []
    property var downloader: null
    readonly property var downloaderObject: root.downloader
        ? root.downloader
        : ((typeof Downloads !== "undefined") ? Downloads : null)
    // volumeId -> scraped first-page url
    property var coverByVolume: ({})
    // chapterId -> volumeId, so a thumbReady can be routed back to its volume
    property var _thumbWanted: ({})

    // Ask for one thumbnail per volume: the first chapter that falls inside its
    // range. Cheap — one request per volume, not per chapter, and only for volumes
    // that still have no cover.
    function requestCovers() {
        var d = root.downloaderObject
        if (!d || !d.fetchThumb || !root.seriesId.length) return
        var chs = root.chapters || []
        if (!chs.length) return
        var rows = root.volumeRows || []
        // ACCUMULATE, never replace: volumes and chapters land at different times, so
        // this runs more than once per series. Rebuilding the map would orphan the
        // requests still in flight from the previous call and those covers would
        // never arrive.
        var wanted = {}
        for (var k in root._thumbWanted) wanted[k] = root._thumbWanted[k]
        for (var i = 0; i < rows.length; i++) {
            var vid = String(rows[i].id || "")
            if (!vid.length || root.coverByVolume[vid]) continue
            var cid = root._firstChapterIdIn(rows[i], chs)
            if (!cid.length || wanted[cid] === vid) continue   // already asked
            wanted[cid] = vid
            d.fetchThumb(root.seriesId, cid)
        }
        root._thumbWanted = wanted
    }
    // the lowest-numbered live chapter inside [chapterStart, chapterEnd]
    function _firstChapterIdIn(row, chs) {
        var lo = Number(row.chapterStart), hi = Number(row.chapterEnd)
        if (isNaN(lo) || isNaN(hi)) return ""
        var bestId = "", bestNum = Infinity
        for (var i = 0; i < chs.length; i++) {
            var n = Number(chs[i].number)
            if (isNaN(n) || n < lo || n > hi) continue
            if (n < bestNum) { bestNum = n; bestId = String(chs[i].id || "") }
        }
        return bestId
    }

    // The reader records ONE record per series under kind "tankoban", carrying the
    // volume it was left in (chapterId) plus page/max — so "where am I" is a single
    // lookup, not a scan. Re-read whenever the series changes or a record lands.
    property var _resume: null
    function refreshResume() {
        var p = root.progressObject
        root._resume = (p && root.seriesId.length) ? p.get("tankoban", root.seriesId) : null
    }
    readonly property string continueVolumeId:
        (root._resume && root._resume.chapterId) ? String(root._resume.chapterId) : ""
    readonly property int continuePage: (root._resume && root._resume.page) ? Number(root._resume.page) : 0
    readonly property int continueMax: (root._resume && root._resume.max) ? Number(root._resume.max) : 0
    readonly property real continueFraction:
        root.continueMax > 0 ? Math.max(0, Math.min(1, root.continuePage / root.continueMax)) : 0

    // canonical model — every volume the service knows for this series
    property var volumeRows: []
    // the app's owned/watched ink, matching Theatre's watched state
    readonly property color ownedInk: "#8fd6a4"
    // per-volume live download progress { volumeId: {done,total,state} } (reassigned to stay reactive)
    property var progressByVolume: ({})

    // inspection: how many volume rows the Repeater actually instantiated
    readonly property int renderedCount: rowsRepeater.count

    // Opening a downloaded volume in the reader is a later layer; surfaced as a
    // signal so the page can wire it without this surface knowing about readers.
    signal openVolumeRequested(string volumeId)
    // "Choose source" on an undownloaded volume raises this with the volume's
    // identity; MangaSeries merges the series id/title and opens the full-screen
    // MangaTankobanSourcesPage. This surface never renders the picker itself.
    signal sourcesRequested(var context)

    Theme { id: theme }

    implicitHeight: listCol.height
    height: listCol.height

    Component.onCompleted: { root.refresh(); root.refreshResume(); root.requestCovers() }
    onSeriesIdChanged: {
        root.coverByVolume = ({})       // covers belong to the OLD series — drop them
        root._thumbWanted = ({})
        root.refresh(); root.refreshResume(); root.requestCovers()
    }
    // volumes and chapters arrive from different sources at different times; ask
    // again whenever either lands, since a cover needs both.
    onVolumeRowsChanged: root.requestCovers()
    onChaptersChanged: root.requestCovers()

    // route a scraped first-page url back to the volume that asked for it
    Connections {
        target: root.downloaderObject
        ignoreUnknownSignals: true
        function onThumbReady(chapterId, url) {
            var vid = root._thumbWanted[String(chapterId)]
            if (!vid || !url || !String(url).length) return
            root.coverByVolume = root._reassign(root.coverByVolume, vid, String(url))
        }
    }

    // A downloaded volume shows its OWN first page — the real book, not a stand-in.
    // Falls back to the scraped first chapter page, then to the numbered placeholder.
    function coverFor(row) {
        var vid = String(row.id || "")
        if (String(row.state) === "ready") {
            var s = root.serviceObject
            if (s && s.localPages) {
                var lp = s.localPages(vid)
                if (lp && lp.length && lp[0].url) return String(lp[0].url)
            }
        }
        if (row.cover && String(row.cover).length) return String(row.cover)
        return root.coverByVolume[vid] || ""
    }
    // Coming back from the reader must move the Continue row, so re-read on reveal.
    onVisibleChanged: if (visible) root.refreshResume()

    function refresh() {
        var s = root.serviceObject
        root.volumeRows = s ? s.volumesForSeries(root.seriesId) : []
    }
    function _ownsVolume(vid) {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === String(vid)) return true
        return false
    }
    function _reassign(map, key, value) {
        var m = {}
        for (var k in map) m[k] = map[k]
        m[key] = value
        return m
    }
    function _inFlight(st) {
        return st === "resolving" || st === "downloading" || st === "ingesting" || st === "packing"
    }
    function clearProgress(vid) {
        if (root.progressByVolume[vid] === undefined) return
        var m = {}
        for (var k in root.progressByVolume) if (k !== vid) m[k] = root.progressByVolume[k]
        root.progressByVolume = m
    }
    // "Choose source" on an undownloaded volume -> emit the volume's identity so
    // MangaSeries opens the full-screen sources picker. The picker (not this surface)
    // kicks the native searchSources; the row just reflects the resulting in-flight state.
    function chooseSource(vid) {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            if (String(rows[i].id) === String(vid)) {
                root.sourcesRequested({
                    "volumeId": String(vid),
                    "number": rows[i].number,
                    "title": (rows[i].title && String(rows[i].title).length) ? String(rows[i].title) : "",
                    "cover": rows[i].cover ? String(rows[i].cover) : ""
                })
                return
            }
        }
        // Volume not in the current rows (e.g. a reader escape before refresh) — still
        // open the picker keyed on the id; identity fields fill in when volumes land.
        root.sourcesRequested({ "volumeId": String(vid), "number": "", "title": "", "cover": "" })
    }
    // The single per-row action, dispatched off the volume's live (effective) state.
    function primaryAction(rowItem) {
        var st = String(rowItem.effectiveState)
        var vid = rowItem.volumeId
        if (st === "ready") { root.openVolumeRequested(vid); return }
        if (root._inFlight(st)) {
            var s = root.serviceObject
            if (s) s.cancel(vid)
            else if (typeof TankobanVolumes !== "undefined") TankobanVolumes.cancel(vid)
            return
        }
        root.chooseSource(vid)
    }

    Connections {
        target: root.serviceObject
        ignoreUnknownSignals: true
        function onVolumesChanged(sid) { if (sid === root.seriesId) root.refresh() }
        function onProgress(vid, done, total) {
            if (!root._ownsVolume(vid)) return
            var s = root.serviceObject
            var st = (s && s.statusOf) ? String(s.statusOf(vid).state || "downloading") : "downloading"
            if (st === "none" || st === "ready") st = "downloading"
            root.progressByVolume = root._reassign(root.progressByVolume, vid,
                { "done": done, "total": total, "state": st })
        }
        function onFinished(vid) { root.clearProgress(vid); root.refresh() }
        function onFailed(vid, reason) { root.clearProgress(vid); root.refresh() }
        function onRemoved(vid) {
            root.clearProgress(vid)
            root.refresh()
        }
        function onSynopsisReady(vid) { root.refresh() }
    }

    Column {
        id: listCol
        width: root.width
        spacing: 0

        Repeater {
            id: rowsRepeater
            model: root.volumeRows

            delegate: Item {
                id: vrow
                required property var modelData
                width: listCol.width
                implicitHeight: rowMain.height
                height: rowMain.height

                readonly property string volumeId: String(modelData.id || "")
                readonly property var prog: root.progressByVolume[volumeId]
                // effective state: index/acquisition state, but any LIVE progress means
                // this row is acquiring even before volumeRows refreshes.
                readonly property string effectiveState: (prog !== undefined && prog !== null)
                    ? String(prog.state || "downloading")
                    : String(modelData.state || "none")
                readonly property string synopsisText:
                    (modelData.synopsis && String(modelData.synopsis).length) ? String(modelData.synopsis) : ""

                // Theatre's "Next up", one medium over: the volume the reader was left
                // in, taller and gold. Only while it is genuinely part-read — a finished
                // volume is not something to continue.
                readonly property bool isContinue: root.continueVolumeId.length > 0
                    && root.continueVolumeId === vrow.volumeId
                    && root.continueFraction > 0.005 && root.continueFraction < 0.995
                readonly property bool owned: vrow.effectiveState === "ready"
                // one dim uppercase line, exactly as Theatre states an episode
                readonly property string stateWord: {
                    if (vrow.isContinue) return "Continue · page " + root.continuePage
                    if (vrow.effectiveState === "failed") return "Couldn’t finish"
                    if (root._inFlight(vrow.effectiveState)) {
                        var f = vrow.progressFraction()
                        return f >= 0 ? Math.round(f * 100) + "% downloaded" : "Downloading"
                    }
                    if (vrow.owned) return "On this device"
                    return "Available"
                }
                readonly property color stateInk: vrow.isContinue ? theme.gold
                    : (vrow.effectiveState === "failed" ? "#e6a3a3"
                       : (vrow.owned ? root.ownedInk : theme.inkDimmer))
                readonly property string chapterSpan: {
                    var a = vrow.modelData.chapterStart, b = vrow.modelData.chapterEnd
                    if (!a || !String(a).length || !b || !String(b).length) return ""
                    return String(a) === String(b) ? ("Chapter " + a) : ("Chapters " + a + "–" + b)
                }

                function progressFraction() {
                    if (vrow.prog === undefined || vrow.prog === null) return -1
                    var t = Number(vrow.prog.total) || 0
                    if (t <= 0) return -1
                    return (Number(vrow.prog.done) || 0) / t
                }
                function progText(verb) {
                    var f = vrow.progressFraction()
                    return f >= 0 ? (verb + " " + Math.round(f * 100) + "%") : (verb + "…")
                }
                function statusLine() {
                    switch (vrow.effectiveState) {
                    case "ready": return "● Downloaded"
                    case "resolving": return "Finding source…"
                    case "ingesting": return "Adding to library…"
                    case "packing": return vrow.progText("Building")
                    case "downloading": return vrow.progText("Downloading")
                    case "failed": return "Couldn’t finish — choose another source"
                    default: return ""
                    }
                }

                // ── the row: Theatre's episode anatomy, portrait artwork ──
                Item {
                    id: rowMain
                    width: parent.width
                    height: vrow.isContinue ? 196 : 156

                    // hover / continue tint, inset to the page margins like Theatre
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: theme.margin
                        anchors.rightMargin: theme.margin
                        color: vrow.isContinue ? Qt.rgba(0.94, 0.77, 0.29, 0.035)
                             : (rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent")
                    }
                    // Theatre's 2px gold rail on the row you are mid-way through
                    Rectangle {
                        x: theme.margin; width: 2; height: parent.height
                        visible: vrow.isContinue; color: theme.gold
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.leftMargin: theme.margin
                        anchors.right: parent.right; anchors.rightMargin: theme.margin
                        anchors.bottom: parent.bottom; height: 1; color: theme.edge
                    }

                    // ── number rail — carries THE SPINE when the book is on disk ──
                    Item {
                        id: numberRail
                        x: theme.margin + 2
                        width: 70; height: parent.height
                        Column {
                            anchors.centerIn: parent; spacing: 2
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: vrow.modelData.number || "?"
                                color: vrow.isContinue ? theme.gold : theme.ink
                                font.family: theme.display
                                font.pixelSize: vrow.isContinue ? 25 : 21
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "VOL"
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 9
                                font.letterSpacing: 1.1; font.weight: Font.DemiBold
                            }
                        }
                        // a hairline normally; a gold SPINE once the book is yours
                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: vrow.owned ? 2 : 1
                            height: parent.height - 28
                            color: vrow.owned ? theme.gold : theme.edge
                            opacity: vrow.owned ? 0.55 : 1.0
                        }
                    }

                    // ── the book: portrait, where Theatre has a landscape still ──
                    Item {
                        id: cov
                        x: numberRail.x + numberRail.width + 16
                        y: (parent.height - height) / 2
                        width: vrow.isContinue ? 126 : 100
                        height: vrow.isContinue ? 176 : 140
                        Rectangle {
                            anchors.fill: parent; radius: 6; clip: true
                            color: "#15171f"
                            border.width: 1
                            border.color: vrow.owned ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
                            Text {
                                anchors.centerIn: parent
                                visible: coverImg.status !== Image.Ready
                                text: vrow.modelData.number || "?"
                                color: Qt.rgba(1, 1, 1, 0.5)
                                font.family: theme.display
                                font.pixelSize: vrow.isContinue ? 38 : 30
                            }
                            Image {
                                id: coverImg
                                anchors.fill: parent; anchors.margins: 1
                                source: root.coverFor(vrow.modelData)
                                visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true; cache: true
                                sourceSize.width: 280
                            }
                            // a book you own catches the light down its spine edge
                            Rectangle {
                                visible: vrow.owned
                                anchors.left: parent.left; anchors.top: parent.top
                                anchors.bottom: parent.bottom; anchors.margins: 1
                                width: 3
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.28) }
                                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
                                }
                            }
                            // how far into THIS book you are — Theatre's thumbnail bar
                            Rectangle {
                                visible: vrow.isContinue
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 4; color: Qt.rgba(0, 0, 0, 0.5)
                                Rectangle {
                                    width: parent.width * root.continueFraction
                                    height: parent.height; color: theme.gold
                                }
                            }
                        }
                    }

                    // ── name · chapter span · blurb ──
                    Column {
                        anchors.left: cov.right; anchors.leftMargin: 18
                        anchors.right: statusBlock.left; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: vrow.isContinue ? 8 : 5
                        Text {
                            width: parent.width
                            text: (vrow.modelData.title && String(vrow.modelData.title).length)
                                ? vrow.modelData.title : ("Volume " + (vrow.modelData.number || ""))
                            color: theme.ink; font.family: theme.ui
                            font.pixelSize: vrow.isContinue ? 17 : 15
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: vrow.chapterSpan.length > 0
                            text: vrow.chapterSpan
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                        }
                        Text {
                            visible: vrow.synopsisText.length > 0
                            width: parent.width; text: vrow.synopsisText
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                            lineHeight: 1.35; wrapMode: Text.WordWrap
                            maximumLineCount: vrow.isContinue ? 2 : 1
                            elide: Text.ElideRight
                        }
                        // live download bar, only while acquiring with a known total
                        Row {
                            visible: vrow.progressFraction() >= 0
                            spacing: 10
                            Rectangle {
                                width: 180; height: 3; radius: 2
                                anchors.verticalCenter: parent.verticalCenter
                                color: Qt.rgba(1, 1, 1, 0.14)
                                Rectangle {
                                    height: parent.height; radius: 2; color: theme.gold
                                    width: parent.width * Math.max(0, Math.min(1, vrow.progressFraction()))
                                }
                            }
                            Text {
                                text: vrow.statusLine()
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            id: appleAttrib
                            readonly property bool isLink: vrow.modelData.synopsisSource === "apple"
                                && !!vrow.modelData.synopsisSourceUrl
                            visible: vrow.modelData.synopsisSource === "apple"
                            text: "Synopsis via Apple Books"
                            color: (appleAttrib.isLink && attribMa.containsMouse) ? theme.gold : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 11
                            MouseArea {
                                id: attribMa
                                anchors.fill: parent
                                enabled: appleAttrib.isLink
                                hoverEnabled: true
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: if (vrow.modelData.synopsisSourceUrl)
                                    Qt.openUrlExternally(vrow.modelData.synopsisSourceUrl)
                            }
                        }
                    }

                    // ── the state, said once, in Theatre's uppercase ──
                    Row {
                        id: statusBlock
                        anchors.right: rowActions.left; anchors.rightMargin: 18
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        visible: rowMain.width > 900
                        PlayerIcon {
                            visible: vrow.owned && !vrow.isContinue
                            width: 15; height: 15; kind: "check"; ink: root.ownedInk
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: vrow.stateWord
                            color: vrow.stateInk
                            font.family: theme.ui; font.pixelSize: 10
                            font.letterSpacing: 1.1; font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // ── circular actions, Theatre's 38px pair ──
                    Row {
                        id: rowActions
                        anchors.right: parent.right; anchors.rightMargin: theme.margin + 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Rectangle {
                            width: 38; height: 38; radius: 19
                            color: primMa.containsMouse ? theme.ink : Qt.rgba(1, 1, 1, 0.07)
                            border.width: 1
                            border.color: vrow.isContinue ? theme.gold : theme.edge
                            PlayerIcon {
                                anchors.centerIn: parent
                                width: 16; height: 16
                                kind: vrow.owned ? "play"
                                      : (root._inFlight(vrow.effectiveState) ? "cancel" : "download")
                                ink: primMa.containsMouse ? "#111111"
                                     : (vrow.isContinue ? theme.gold : theme.ink)
                            }
                            MouseArea {
                                id: primMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.primaryAction(vrow)
                            }
                        }
                        // inert owned marker — the slot Theatre gives the download button
                        Rectangle {
                            visible: vrow.owned
                            width: 38; height: 38; radius: 19
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1; border.color: root.ownedInk
                            PlayerIcon {
                                anchors.centerIn: parent
                                width: 16; height: 16; kind: "check"; ink: root.ownedInk
                            }
                        }
                    }

                    MouseArea {
                        id: rowMa; anchors.fill: parent; hoverEnabled: true; z: -1
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.primaryAction(vrow)
                    }
                }

            }
        }
    }
}
