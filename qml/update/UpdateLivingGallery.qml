pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import "../"

// The release chronicle is deliberately one stage: verified local art, a monochrome wash,
// directional scrims, and editorial copy. State actions live in the persistent taskbar.
Item {
    id: root
    objectName: "colosseumUpdateGallery"

    property var release: ({})
    property var chapters: []
    property int currentIndex: 0
    property bool reducedMotion: false
    property int taskbarSafeBottomLane: 96
    readonly property int automationLogicalWidth: Math.round(width)
    readonly property int automationLogicalHeight: Math.round(height)
    readonly property real automationDevicePixelRatio: Screen.devicePixelRatio
    readonly property real automationStageOpacity: monochromeStage.opacity
    readonly property bool automationStageSettled: automationStageOpacity >= 0.99
    property bool automationVisualReady: false
    readonly property bool visualContentReady: stageImage.status === Image.Ready
                                               && automationStageSettled
                                               && chapterTitle.visible && chapterTitle.text.length > 0
                                               && chapterBody.visible && chapterBody.text.length > 0
                                               && chapterNavigation.visible

    readonly property bool imageCrossfadeEnabled: !reducedMotion
    readonly property var normalizedChapters: {
        var source = chapters || []
        var result = []
        for (var i = 0; i < source.length; i++) {
            var candidate = source[i] || ({})
            var kind = String(candidate.kind || "feature")
            if (kind === "feature" || kind === "statistic" || kind === "beforeAfter"
                    || kind === "milestone")
                result.push(candidate)
        }
        if (result.length === 0) {
            result.push({ section: "RELEASE", title: String(release.title || "The latest chapter"),
                          body: String(release.summary || "The latest Colosseum chronicle lives here."),
                          artwork: [] })
        }
        return result
    }
    readonly property int chapterCount: normalizedChapters.length
    readonly property var selectedChapter: normalizedChapters[Math.max(0, Math.min(currentIndex,
                                                                                   chapterCount - 1))]
    readonly property string selectedArtwork: {
        var art = selectedChapter && selectedChapter.artwork ? selectedChapter.artwork : []
        return art && art.length > 0 ? String(art[0] || "") : ""
    }
    readonly property bool hasArtwork: selectedArtwork.length > 0
    readonly property string versionText: String(release.version || "")
    readonly property string fallbackSource: Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")
    // The back capsule owns the first 86px of the chrome row. Keep the release masthead in
    // its own editorial lane so the navigation control never sits on top of its lettering.
    readonly property int chromeSafeLeft: 126

    signal chapterRequested(int index)
    signal nextRequested()
    signal primaryClicked()
    signal cancelClicked()

    function selectChapter(index) {
        if (chapterCount < 1) return
        var next = Math.max(0, Math.min(Number(index), chapterCount - 1))
        if (next === currentIndex) return
        currentIndex = next
        chapterRequested(currentIndex)
    }
    function nextChapter() {
        if (chapterCount < 2) return
        currentIndex = (currentIndex + 1) % chapterCount
        nextRequested()
        chapterRequested(currentIndex)
    }
    function moveChapter(delta) {
        if (chapterCount < 2) return
        selectChapter((currentIndex + delta + chapterCount) % chapterCount)
    }

    onChapterCountChanged: {
        if (currentIndex >= chapterCount)
            currentIndex = Math.max(0, chapterCount - 1)
    }
    onVisibleChanged: if (!visible) automationVisualReady = false
    onCurrentIndexChanged: automationVisualReady = false
    onVisualContentReadyChanged: if (!visualContentReady) automationVisualReady = false

    FrameAnimation {
        running: root.visible && root.visualContentReady && !root.automationVisualReady
        onTriggered: root.automationVisualReady = true
    }

    Theme { id: theme }

    // This is the only art texture in the stage. The effect draws the monochrome copy; the source
    // image is kept hidden so the real screenshot is never duplicated or blurred.
    Image {
        id: stageImage
        anchors.fill: parent
        source: root.hasArtwork ? root.selectedArtwork : root.fallbackSource
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        visible: false
        sourceSize.width: Math.max(2, Math.round(root.width))
        sourceSize.height: Math.max(2, Math.round(root.height))
    }
    MultiEffect {
        id: monochromeStage
        anchors.fill: parent
        source: stageImage
        saturation: -1.0
        opacity: stageImage.status === Image.Ready ? 1 : 0
        Behavior on opacity {
            enabled: root.imageCrossfadeEnabled
            NumberAnimation { duration: 220; easing.type: Easing.InOutQuad }
        }
    }
    Item {
        objectName: "colosseumUpdateGalleryFallbackArt"
        anchors.fill: parent
        visible: !root.hasArtwork
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.82) }
            GradientStop { position: 0.43; color: Qt.rgba(0, 0, 0, 0.34) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.08) }
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.50) }
            GradientStop { position: 0.58; color: Qt.rgba(0, 0, 0, 0.02) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.70) }
        }
    }

    // Quiet metadata at the top: no marketing subtitle sits beneath the release version.
    Text {
        objectName: "colosseumUpdateReleaseLabel"
        anchors.left: parent.left
        anchors.leftMargin: root.chromeSafeLeft
        anchors.top: parent.top
        anchors.topMargin: 30
        text: "COLOSSEUM UPDATE"
        color: Qt.rgba(1, 1, 1, 0.72)
        font.family: "Inter"
        font.pixelSize: 11
        font.letterSpacing: 2.7
        font.weight: Font.DemiBold
    }
    Text {
        id: versionTitle
        objectName: "colosseumUpdateVersionTitle"
        anchors.left: parent.left
        anchors.leftMargin: root.chromeSafeLeft
        anchors.top: parent.top
        anchors.topMargin: 47
        text: root.versionText.length > 0 ? root.versionText : String(root.release.title || "")
        color: "#ffffff"
        font.family: theme.display
        font.pixelSize: Math.max(38, Math.min(70, root.width * 0.055))
        font.letterSpacing: -1.0
    }
    Item {
        id: chapterCopy
        objectName: "colosseumUpdateChapterCopy"
        anchors.left: parent.left
        anchors.leftMargin: 54
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.taskbarSafeBottomLane + chapterNavigation.height + 24
        width: Math.min(Math.max(320, root.width * 0.46), 620)
        height: copyColumn.implicitHeight

        Column {
            id: copyColumn
            width: parent.width
            spacing: 8
            Text {
                objectName: "colosseumUpdateChapterSection"
                text: String(root.selectedChapter.section || "RELEASE")
                color: Qt.rgba(1, 1, 1, 0.72)
                font.family: "Inter"
                font.pixelSize: 11
                font.letterSpacing: 2.4
                font.weight: Font.DemiBold
            }
            Text {
                id: chapterTitle
                objectName: "colosseumUpdateChapterTitle"
                text: String(root.selectedChapter.title || root.release.title || "The latest chapter")
                color: "#ffffff"
                font.family: theme.display
                font.pixelSize: Math.max(34, Math.min(58, root.width * 0.045))
                font.letterSpacing: -0.7
                wrapMode: Text.WordWrap
                width: parent.width
            }
            Text {
                id: chapterBody
                objectName: "colosseumUpdateChapterBody"
                text: String(root.selectedChapter.body || root.release.summary || "")
                color: Qt.rgba(1, 1, 1, 0.84)
                font.family: "Inter"
                font.pixelSize: 15
                lineHeight: 1.22
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }
    }

    Item {
        id: chapterNavigation
        objectName: "colosseumUpdateChapterNav"
        anchors.left: parent.left
        anchors.leftMargin: 54
        anchors.right: parent.right
        anchors.rightMargin: 54
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.taskbarSafeBottomLane
        height: 44

        Row {
            id: chapterNavigator
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Text {
                objectName: "colosseumUpdateChapterCount"
                text: String(root.currentIndex + 1).padStart(2, "0") + " / "
                      + String(root.chapterCount).padStart(2, "0")
                color: Qt.rgba(1, 1, 1, 0.94)
                font.family: theme.display
                font.pixelSize: 20
            }
            Text {
                objectName: "colosseumUpdateChapterLabel"
                visible: root.chapterCount > 1
                text: String(root.selectedChapter.section || "RELEASE")
                color: Qt.rgba(1, 1, 1, 0.62)
                font.family: "Inter"
                font.pixelSize: 9
                font.letterSpacing: 1.8
                font.weight: Font.DemiBold
            }
            Rectangle {
                visible: root.chapterCount > 1
                width: 1
                height: 18
                color: Qt.rgba(1, 1, 1, 0.26)
            }
            Repeater {
                model: root.normalizedChapters
                delegate: Item {
                    required property var modelData
                    required property int index
                    objectName: "colosseumUpdateChapter_%1".arg(String(index + 1).padStart(2, "0"))
                    width: 44
                    height: 44
                    visible: root.chapterCount > 1
                    focus: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Chapter %1: %2".arg(index + 1).arg(String(modelData.title || "Chapter"))
                    Accessible.description: index === root.currentIndex ? "Selected" : ""
                    Text {
                        anchors.centerIn: parent
                        text: String(index + 1).padStart(2, "0")
                        color: Qt.rgba(1, 1, 1, index === root.currentIndex ? 0.98 : 0.56)
                        font.family: "Inter"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        width: index === root.currentIndex ? 16 : 0
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.92)
                        Behavior on width {
                            enabled: !root.reducedMotion
                            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectChapter(index)
                    }
                    Keys.onReturnPressed: root.selectChapter(index)
                    Keys.onSpacePressed: root.selectChapter(index)
                }
            }
            Rectangle {
                id: nextChapter
                objectName: "colosseumUpdateNextChapter"
                visible: root.chapterCount > 1
                width: 44
                height: 44
                radius: 22
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, enabled ? 0.62 : 0.22)
                enabled: root.chapterCount >= 2
                focus: true
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Next chapter"
                Text {
                    id: nextText
                    anchors.centerIn: parent
                    text: "Next"
                    color: Qt.rgba(1, 1, 1, parent.enabled ? 0.86 : 0.32)
                    font.family: "Inter"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: parent.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.nextChapter()
                }
                Keys.onReturnPressed: root.nextChapter()
                Keys.onSpacePressed: root.nextChapter()
            }
        }
    }

    focus: true
    activeFocusOnTab: true
    Keys.onLeftPressed: root.moveChapter(-1)
    Keys.onRightPressed: root.moveChapter(1)
}
