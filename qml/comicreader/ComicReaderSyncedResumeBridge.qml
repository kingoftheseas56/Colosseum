// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import "ComicReaderImportedResume.js" as ImportedResume

Item {
    id: root
    objectName: "comicReaderSyncedResumeBridge"
    visible: false
    width: 0
    height: 0

    // The authoritative profile-scoped ProgressStore context object.
    property var progress: null

    // Stable logical series identity supplied by the reader shell.
    property string seriesId: ""

    // Latest validated remote Tankoban resume waiting for the shell's existing
    // reader-ready restore path.  The bridge never mutates page/surface state
    // directly and therefore cannot invent a second resume implementation.
    readonly property var pendingTarget: _pendingTarget
    property var _pendingTarget: null
    property string _lastFingerprint: ""

    signal resumeRequested(var target)
    signal rejected(string code)

    function clearPending() {
        _pendingTarget = null
    }

    function acceptPending(target) {
        if (!target || !_pendingTarget)
            return
        if (ImportedResume.fingerprint(target)
                === ImportedResume.fingerprint(_pendingTarget))
            _pendingTarget = null
    }

    function _handleImported(kind, id) {
        if (!progress || !seriesId)
            return
        if (String(kind) !== "tankoban"
                || String(id) !== String(seriesId))
            return

        var record = progress.get("tankoban", seriesId)
        var target = ImportedResume.resolve(record, seriesId)
        if (!target.valid) {
            // Never leave an older imported target pending after the owner has
            // advanced to a record we refuse to apply.
            _pendingTarget = null
            rejected(target.code)
            return
        }

        var fp = ImportedResume.fingerprint(target)
        if (!fp || fp === _lastFingerprint)
            return

        _lastFingerprint = fp
        _pendingTarget = target
        resumeRequested(target)
    }

    Connections {
        target: root.progress
        enabled: root.progress !== null

        function onSyncedEntryApplied(kind, id) {
            root._handleImported(kind, id)
        }
    }
}
