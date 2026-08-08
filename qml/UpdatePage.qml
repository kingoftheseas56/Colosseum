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
        case 1: return "Checking for updates"
        case 2: return "No updates available"
        case 3: return "Download update"
        case 4: return "Downloading update"
        case 5: return "Resume download"
        case 6: return "Verifying update"
        case 7: return "Restart and update"
        case 8: return "Installing update"
        case 9: return "Retry download"
        case 10: return "Check for a newer release"
        case 11: return "Manual update required"
        default: return "Check for updates"
        }
    }
    function statusCopy(value) {
        switch (Number(value)) {
        case 1: return "Checking for updates"
        case 2: return "Up to date"
        case 3: return "Update available"
        case 4: return "Downloading update"
        case 5: return "Download paused"
        case 6: return "Verifying update"
        case 7: return "Ready to restart"
        case 8: return "Installing update"
        case 9: return "Try the download again"
        case 10: return "This release could not be verified"
        case 11: return "Install this release manually from GitHub"
        default: return "No update check yet"
        }
    }
    function actionEnabled(value) {
        return Number(value) !== 1 && Number(value) !== 4
                && Number(value) !== 6 && Number(value) !== 8
    }
    function invokePrimaryAction() {
        if (!updates || !actionEnabled(updates.state)) return
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

    // swallow clicks so a full-page chronicle never leaks into the surface below it
    MouseArea { anchors.fill: parent; z: -1 }
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image {
            anchors.fill: parent
            visible: root.backdrop === null
            source: "../assets/wallpaper/captured-motion.jpg"
            fillMode: Image.PreserveAspectCrop
            cache: true
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.02, 0.025, 0.04, 0.88) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        anchors.topMargin: 86
        anchors.bottomMargin: 30
        contentWidth: width
        contentHeight: contentColumn.implicitHeight + 80
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: contentColumn
            x: theme.margin
            width: root.width - theme.margin * 2
            spacing: 22

            Text {
                text: "COLOSSEUM · UPDATE CHRONICLE"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
                font.letterSpacing: 2.6
                font.weight: Font.DemiBold
            }
            Text {
                text: "Keep the house current"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 56
                font.letterSpacing: -1
                wrapMode: Text.WordWrap
                width: parent.width
            }
            Text {
                id: statusText
                objectName: "colosseumUpdateStatusText"
                text: root.statusCopy(updates ? updates.state : 0)
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 14
            }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            Flow {
                id: releaseFlow
                width: parent.width
                spacing: 22

                UpdateUi.UpdateReleaseHero {
                    width: root.width >= 980 ? (releaseFlow.width * 0.54) : releaseFlow.width
                    release: root.releaseModel
                    stateLabel: root.statusCopy(updates ? updates.state : 0)
                    primaryLabel: root.primaryLabel
                    primaryEnabled: root.actionEnabled(updates ? updates.state : 0)
                    cancelVisible: updates && Number(updates.state) === 4
                    progress: updates ? Number(updates.progress || 0) : 0
                    reducedMotion: root.reducedMotion
                    onPrimaryClicked: root.invokePrimaryAction()
                    onCancelClicked: root.cancelDownload()
                }

                Column {
                    objectName: "colosseumUpdateHighlights"
                    width: root.width >= 980 ? (releaseFlow.width * 0.42) : releaseFlow.width
                    spacing: 14
                    Repeater {
                        objectName: "colosseumUpdateHighlightRepeater"
                        model: root.highlightModel
                        delegate: UpdateUi.UpdateHighlightCard {
                            required property var modelData
                            required property int index
                            width: parent.width
                            cardIndex: index
                            highlight: modelData
                        }
                    }
                    Text {
                        visible: root.highlightModel.length === 0
                        text: "The latest chronicle is kept here as new releases arrive."
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                }
            }
        }
    }

    BackAction {
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
