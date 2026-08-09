pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "update" as UpdateUi

Item {
    id: root
    objectName: "colosseumUpdatePage"
    property Item backdrop: null
    property var updates: null
    property bool reducedMotion: false
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    // The native enum is deliberately mapped here to stable human language. QML never sees
    // error codes or transport logs, and the test seam can use the same numeric values.
    readonly property string automationState: stateName(updates ? updates.state : 0)
    readonly property string automationVersion: updates ? String(updates.latestVersion || "") : ""
    readonly property string primaryLabel: primaryCopy(updates ? updates.state : 0)
    readonly property bool primaryVisible: actionEnabled(updates ? updates.state : 0)
    readonly property bool progressVisible: isProgressState(updates ? updates.state : 0)
    readonly property bool progressIndeterminate: progressVisible && Number(updates ? updates.totalBytes : 0) <= 0
    readonly property real progressValue: updates ? Number(updates.progress || 0) : 0
    readonly property string progressText: formatProgress(updates ? updates.receivedBytes : 0,
                                                          updates ? updates.totalBytes : 0,
                                                          progressValue)
    readonly property string taskbarMetadata: metadataCopy(updates ? updates.state : 0)
    readonly property var taskbarPresentation: ({
        statusText: statusCopy(updates ? updates.state : 0),
        metadataText: taskbarMetadata,
        progressText: progressText,
        progress: progressValue,
        progressVisible: progressVisible,
        progressIndeterminate: progressIndeterminate,
        primaryLabel: primaryLabel,
        primaryVisible: primaryVisible,
        primaryEnabled: primaryVisible,
        reducedMotion: reducedMotion
    })
    readonly property var releaseModel: updates && updates.release ? updates.release : ({})
    readonly property var highlightModel: {
        var source = updates && updates.highlights ? updates.highlights : []
        var result = []
        for (var i = 0; i < source.length; i++) {
            var kind = String(source[i].kind || "")
            if (kind === "feature" || kind === "statistic"
                    || kind === "beforeAfter" || kind === "milestone")
                result.push(source[i])
        }
        return result
    }
    // Bumped on each offered-release identity flip (installed chronicle <->
    // newer release). The gallery watches this token to reset its chapter
    // cursor and re-arm the stage crossfade on the same-chapter-count swap
    // (5<->5) that a chapter-count change cannot signal.
    property int offeredReleaseToken: 0
    Connections {
        target: root.updates
        function onOfferedReleaseChanged() { root.offeredReleaseToken++ }
    }

    function stateName(value) {
        switch (Number(value)) {
        case 1: return "Checking"
        case 2: return "UpToDate"
        case 3: return "Available"
        case 4: return "Downloading"
        case 5: return "Paused"
        case 6: return "Verifying"
        case 7: return "Ready"
        case 8: return "Installing"
        case 9: return "RecoverableError"
        case 10: return "VerificationFailure"
        case 11: return "ManualUpdateRequired"
        default: return "Idle"
        }
    }
    function primaryCopy(value) {
        switch (Number(value)) {
        case 1: return ""
        case 2: return "Check again"
        case 3: return "Download update"
        case 4: return "Pause download"
        case 5: return "Resume download"
        case 6: return ""
        case 7: return "Restart and update"
        case 8: return ""
        case 9: return "Retry download"
        case 10: return "Check again"
        case 11: return "Check again"
        default: return "Check again"
        }
    }
    function statusCopy(value) {
        switch (Number(value)) {
        case 1: return "Checking for updates"
        case 2: return "Everything is up to date"
        case 3: return "Colosseum " + targetVersion() + " is ready"
        case 4: return "Updating to " + targetVersion()
        case 5: return "Update paused"
        case 6: return "Verifying the update"
        case 7: return "Ready to enter " + targetVersion()
        case 8: return "Colosseum is updating"
        case 9: return "The update could not finish"
        case 10: return "This update could not be verified"
        case 11: return "Manual update required"
        default: return "No update check yet"
        }
    }
    function actionEnabled(value) {
        return Number(value) !== 1 && Number(value) !== 6 && Number(value) !== 8
    }
    function isProgressState(value) {
        return Number(value) === 4 || Number(value) === 5
    }
    function targetVersion() {
        return String(updates ? (updates.latestVersion || updates.installedVersion || "") : "")
    }
    function formatBytes(bytes) {
        return Math.round(Math.max(0, Number(bytes) || 0) / 1048576) + " MB"
    }
    function formatProgress(received, total, progress) {
        if (Number(total) <= 0)
            return Number(received) > 0 ? formatBytes(received) + " downloaded \u00b7 size unknown"
                                        : "Download size unknown"
        return formatBytes(received) + " of " + formatBytes(total) + " \u00b7 "
                + Math.round(Math.max(0, Math.min(1, Number(progress) || 0)) * 100) + "%"
    }
    function metadataCopy(value) {
        var installed = String(updates ? (updates.installedVersion || "") : "")
        var latest = targetVersion()
        if (Number(value) === 2)
            return "Installed " + installed + " \u00b7 Latest " + (latest || installed)
        if (Number(value) === 4)
            return ""
        if (latest.length > 0)
            return "Target " + latest
        return installed.length > 0 ? "Installed " + installed : ""
    }
    function invokePrimaryAction() {
        if (!updates || !actionEnabled(updates.state)) return
        if (Number(updates.state) === 4) { updates.cancelDownload(); return }
        if (Number(updates.state) === 3) { updates.download(); return }
        if (Number(updates.state) === 5) { updates.download(); return }
        if (Number(updates.state) === 7) { updates.restartAndUpdate(); return }
        if (Number(updates.state) === 9) { updates.download(); return }
        updates.checkNow()
    }
    function cancelDownload() {
        if (updates && Number(updates.state) === 4)
            updates.cancelDownload()
    }

    Theme { id: theme }

    // The living gallery owns the full stage; window chrome stays above it and keeps the existing
    // Back/Escape/minimize/fullscreen/close contracts.
    UpdateUi.UpdateLivingGallery {
        id: livingGallery
        anchors.fill: parent
        release: root.releaseModel
        chapters: root.highlightModel
        reducedMotion: root.reducedMotion
        taskbarSafeBottomLane: 96
        offeredReleaseToken: root.offeredReleaseToken
    }

    BackAction {
        objectName: "colosseumUpdateBackAction"
        variant: "capsule"
        tip: "Back"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 21
        anchors.leftMargin: theme.margin - 10
        onTriggered: root.backRequested()
    }
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.rightMargin: theme.margin
        width: chromeRow.implicitWidth
        height: 30
        Row {
            id: chromeRow
            spacing: 22
            Text { text: "—"; color: minMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                               onClicked: root.minimizeRequested() } }
            Text { text: "⛶"; color: fullMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: fullMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                               onClicked: root.fullscreenRequested() } }
            Text { text: "⏻"; color: powerMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: powerMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                               onClicked: root.closeRequested() } }
        }
    }

    focus: true
    Shortcut {
        objectName: "colosseumUpdateEscape"
        sequence: "Escape"
        onActivated: root.backRequested()
    }
    Keys.onEscapePressed: root.backRequested()
}
