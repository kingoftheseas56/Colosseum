// GuidedAnalysisDetails — the one-line "how is analysis going" status for the Guided
// Reader. It reads the job summary the analysis service publishes (stage + ready/total)
// and, when a canvas fell back or failed, names why in plain words. Task 10 gives it the
// job-level line; Task 11 enriches it with per-canvas detail + override affordances.

import QtQuick
import "../"   // Theme (lives in qml/, the parent of qml/guided/)

Item {
    id: root

    // Job-level summary from GuidedAnalysis.jobSummary(entryId): { stage, ready, total, paused }
    property var summary: ({})
    // Current-canvas detail (Task 11): { stage, reason }. Empty until wired.
    property var canvasDetail: ({})

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight

    Theme { id: theme }

    readonly property string _stage: (summary && summary.stage) ? String(summary.stage) : ""
    readonly property int _ready: (summary && summary.ready !== undefined) ? summary.ready : 0
    readonly property int _total: (summary && summary.total !== undefined) ? summary.total : 0
    readonly property string _reason: (canvasDetail && canvasDetail.reason) ? String(canvasDetail.reason) : ""

    // Whole-page fallback is a usable result, not a failure — say so gently. Only real
    // operational codes read as trouble.
    function _reasonWords(code) {
        switch (code) {
        case "": return ""
        case "no_panels":
        case "layout_ambiguous":
        case "spread_uncertain": return "whole page"
        case "image_decode_failed": return "couldn't read image"
        case "model_missing": return "detector unavailable"
        case "model_checksum_failed": return "detector invalid"
        case "inference_failed": return "detection failed"
        case "store_failed": return "couldn't save"
        default: return code
        }
    }

    function _line() {
        var words = _reasonWords(root._reason)
        if (root._total > 0) {
            var base = root._ready + " / " + root._total + " pages ready"
            return root._stage ? (base + " · " + root._stage) : base
        }
        if (words.length) return words
        if (root._stage.length) return root._stage
        return ""   // no analysis service reporting yet — say nothing rather than a false "Analyzing…"
    }

    Text {
        id: label
        text: root._line()
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 12
        elide: Text.ElideRight
    }
}
