// MangaTankobanLibrary — the volume-first surface shown in place of the chapter
// shelf when TANKOBAN MODE is ON for a series.
//
// A series IS its volumes here: EVERY canonical volume the service knows renders
// as a row (cover, Vol. N, title, synopsis when present, live state + real
// progress bar, and ONE action — Downloaded->Open, Available->Choose source,
// Downloading%->Cancel). Tapping an undownloaded volume opens an inline source
// chooser BENEATH that row: the ranked Nyaa MangaTankobanSourceCards in the
// service's order, then the quieter WeebCentral card LAST.
//
// The signature: ONE thin GOLD bookbinding rule runs down the LEFT edge, binding
// the run. Restrained — it is not a new theme; gold stays a sparing accent.
//
// Service seam: in the app `service` is the native `TankobanVolumes` context
// property; the offscreen harness injects a fake exposing the same API. All calls
// resolve through `serviceObject`, falling back to the context property so the
// running app needs no wiring.
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

    // canonical model — every volume the service knows for this series
    property var volumeRows: []
    // per-volume live download progress { volumeId: {done,total,state} } (reassigned to stay reactive)
    property var progressByVolume: ({})
    // per-volume cached source results from searchSources — order preserved VERBATIM
    property var expandedByVolume: ({})
    // per-volume chooser open/closed state
    property var openVolumes: ({})

    // inspection: how many volume rows the Repeater actually instantiated
    readonly property int renderedCount: rowsRepeater.count

    // Opening a downloaded volume in the reader is a later layer; surfaced as a
    // signal so the page can wire it without this surface knowing about readers.
    signal openVolumeRequested(string volumeId)

    Theme { id: theme }

    implicitHeight: listCol.height
    height: listCol.height

    Component.onCompleted: root.refresh()
    onSeriesIdChanged: root.refresh()

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
    // Tap an undownloaded volume -> toggle its inline chooser; first open kicks a search.
    function chooseSource(vid) {
        var open = !(root.openVolumes[vid] === true)
        root.openVolumes = root._reassign(root.openVolumes, vid, open)
        if (open && root.expandedByVolume[vid] === undefined) {
            var s = root.serviceObject
            if (s) s.searchSources(vid)
            else if (typeof TankobanVolumes !== "undefined") TankobanVolumes.searchSources(vid)
        }
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
    // A chosen source card -> the matching native call, then collapse the chooser.
    function dispatchSource(vid, cardData) {
        var s = root.serviceObject
        if (cardData && cardData.kind === "weebcentral") {
            if (s) s.compileWeebCentral(vid)
            else if (typeof TankobanVolumes !== "undefined") TankobanVolumes.compileWeebCentral(vid)
        } else if (cardData) {
            if (s) s.downloadNyaa(vid, cardData.infoHash)
            else if (typeof TankobanVolumes !== "undefined") TankobanVolumes.downloadNyaa(vid, cardData.infoHash)
        }
        root.openVolumes = root._reassign(root.openVolumes, vid, false)
    }

    Connections {
        target: root.serviceObject
        ignoreUnknownSignals: true
        function onVolumesChanged(sid) { if (sid === root.seriesId) root.refresh() }
        function onSourcesReady(vid, results) {
            if (!root._ownsVolume(vid)) return
            root.expandedByVolume = root._reassign(root.expandedByVolume, vid, results)
        }
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
            root.openVolumes = root._reassign(root.openVolumes, vid, false)
            root.refresh()
        }
        function onSynopsisReady(vid) { root.refresh() }
    }

    // ── the single thin GOLD bookbinding rule down the LEFT edge ──
    Rectangle {
        id: bindingRule
        x: theme.margin - 14
        y: 6
        width: 2
        height: Math.max(0, listCol.height - 12)
        color: theme.gold
        opacity: 0.85
        visible: root.volumeRows.length > 0
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
                implicitHeight: rowBody.height
                height: rowBody.height

                readonly property string volumeId: String(modelData.id || "")
                readonly property var prog: root.progressByVolume[volumeId]
                // effective state: index/acquisition state, but any LIVE progress means
                // this row is acquiring even before volumeRows refreshes.
                readonly property string effectiveState: (prog !== undefined && prog !== null)
                    ? String(prog.state || "downloading")
                    : String(modelData.state || "none")
                readonly property bool isOpen: root.openVolumes[volumeId] === true
                readonly property var sources: root.expandedByVolume[volumeId] || []
                readonly property string synopsisText:
                    (modelData.synopsis && String(modelData.synopsis).length) ? String(modelData.synopsis) : ""

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
                function actionLabel() {
                    if (vrow.effectiveState === "ready") return "Open"
                    if (root._inFlight(vrow.effectiveState)) return "Cancel"
                    return "Choose source"
                }
                function actionGlyph() {
                    if (vrow.effectiveState === "ready") return "▶"
                    if (root._inFlight(vrow.effectiveState)) return "✕"
                    return "↓"
                }

                Column {
                    id: rowBody
                    width: parent.width

                    // ── the volume row ──
                    Item {
                        id: rowMain
                        width: parent.width; height: 148
                        Rectangle { anchors.fill: parent
                            color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent" }

                        // cover (portrait; numbered placeholder until the pixels arrive)
                        Item {
                            id: cov
                            anchors.left: parent.left; anchors.leftMargin: theme.margin
                            anchors.verticalCenter: parent.verticalCenter
                            width: 88; height: 124
                            Rectangle {
                                anchors.fill: parent; radius: 6; color: "#15171f"; border.width: 1
                                border.color: vrow.effectiveState === "ready" ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                                Text { anchors.centerIn: parent; visible: coverImg.status !== Image.Ready
                                    text: vrow.modelData.number || "?"; color: theme.inkDimmer
                                    font.family: theme.display; font.pixelSize: 30 }
                            }
                            Image {
                                id: coverImg; anchors.fill: parent; anchors.margins: 1
                                source: vrow.modelData.cover ? vrow.modelData.cover : ""
                                visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                            }
                        }

                        // Vol. N · title · synopsis · status/progress
                        Column {
                            anchors.left: cov.right; anchors.leftMargin: 16
                            anchors.right: act.left; anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter; spacing: 5
                            Text {
                                text: "Vol. " + (vrow.modelData.number || "")
                                color: theme.gold; font.family: theme.display
                                font.pixelSize: 13; font.letterSpacing: 1
                            }
                            Text {
                                width: parent.width
                                text: (vrow.modelData.title && String(vrow.modelData.title).length)
                                    ? vrow.modelData.title : ("Volume " + (vrow.modelData.number || ""))
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 17
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: vrow.synopsisText.length > 0
                                width: parent.width; text: vrow.synopsisText
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                                lineHeight: 1.35; wrapMode: Text.WordWrap
                                maximumLineCount: 2; elide: Text.ElideRight
                            }
                            // Apple synopsis attribution — only when Apple-sourced. When the service
                            // forwards a URL it becomes a restrained link (monochrome, gold on hover).
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
                            Text {
                                visible: vrow.statusLine().length > 0
                                text: vrow.statusLine()
                                color: vrow.effectiveState === "ready" ? theme.gold
                                    : (vrow.effectiveState === "failed" ? "#e6a3a3" : theme.inkDimmer)
                                font.family: theme.ui; font.pixelSize: 12
                            }
                            // real progress bar (only while acquiring with a known total)
                            Rectangle {
                                visible: vrow.progressFraction() >= 0
                                width: Math.min(parent.width, 260); height: 3; radius: 2
                                color: Qt.rgba(1, 1, 1, 0.14)
                                Rectangle {
                                    height: parent.height; radius: 2; color: theme.gold
                                    width: parent.width * Math.max(0, Math.min(1, vrow.progressFraction()))
                                }
                            }
                        }

                        // the ONE action
                        Rectangle {
                            id: act
                            anchors.right: parent.right; anchors.rightMargin: theme.margin
                            anchors.verticalCenter: parent.verticalCenter
                            width: actRow.implicitWidth + 26; height: 32; radius: 8
                            color: actMa.containsMouse ? theme.glassHi : theme.glassTint
                            border.width: 1
                            border.color: actMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                            Row {
                                id: actRow; anchors.centerIn: parent; spacing: 7
                                Text { text: vrow.actionGlyph(); color: theme.ink; font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: vrow.actionLabel(); color: theme.inkDim; font.family: theme.ui
                                    font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea {
                                id: actMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.primaryAction(vrow)
                            }
                        }

                        // clicking anywhere on the row runs the primary action too
                        MouseArea {
                            id: rowMa; anchors.fill: parent; hoverEnabled: true; z: -1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.primaryAction(vrow)
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                            color: Qt.rgba(1, 1, 1, 0.05) }
                    }

                    // ── inline source chooser (beneath the row) ──
                    Item {
                        id: chooser
                        width: parent.width
                        visible: vrow.isOpen
                        height: visible ? chooserCol.height + 14 : 0
                        Column {
                            id: chooserCol
                            x: theme.margin; y: 4
                            width: parent.width - theme.margin - 24
                            spacing: 8
                            Repeater {
                                model: vrow.isOpen ? vrow.sources : []
                                delegate: Item {
                                    id: srcDelegate
                                    required property var modelData
                                    width: chooserCol.width
                                    height: srcCard.height
                                    MangaTankobanSourceCard {
                                        id: srcCard
                                        width: parent.width
                                        modelData: srcDelegate.modelData
                                        volumeId: vrow.volumeId
                                        onChosen: root.dispatchSource(vrow.volumeId, srcDelegate.modelData)
                                    }
                                }
                            }
                            Text {
                                visible: vrow.isOpen && vrow.sources.length === 0
                                text: "Searching sources…"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }
}
