import QtQuick 2.15
import QtQuick.Controls.Basic 2.15 as Basic

Rectangle {
    id: root
    objectName: "guideFirstJourney"
    color: "#161616"
    border.color: "#363636"
    border.width: 1
    radius: 2
    implicitHeight: journeyColumn.implicitHeight + 28

    property var progress: null
    readonly property var stepIds: ["meet-worlds", "taskbar-and-library", "choose-wallpaper",
                                    "sources-are-optional", "open-media-and-return"]
    readonly property var steps: [
        { title: "Meet the worlds", body: "Tankoban, Biblio and Theatre each keep a different kind of media close." },
        { title: "Find your way back", body: "The taskbar shows open sessions. Continue remembers progress, while Library is where you deliberately save something." },
        { title: "Choose a wallpaper", body: "Try one built-in wallpaper. It is safe to change again whenever you want." },
        { title: "Keep sources optional", body: "Sources are optional. Reading is download-first, so your local copy stays useful offline." },
        { title: "Open local media", body: "Open Media and Vault help with files already on this device. Return here whenever you need a reminder." }
    ]
    property int currentStep: 0
    readonly property bool complete: currentStep >= steps.length

    function advance() {
        if (currentStep < steps.length - 1) currentStep++
        else currentStep = steps.length
    }

    function completeCurrent() {
        if (currentStep >= steps.length) return
        if (progress && progress.complete) progress.complete(stepIds[currentStep])
        advance()
    }

    function skipCurrent() { completeCurrent() }

    function replay() {
        if (progress && progress.resetJourney) progress.resetJourney()
        currentStep = 0
    }

    Component.onCompleted: {
        if (!progress || !progress.completedSteps) return
        var index = 0
        while (index < stepIds.length && progress.completedSteps.indexOf(stepIds[index]) >= 0) index++
        currentStep = index
    }

    Column {
        id: journeyColumn
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8
        Text { text: "FIRST JOURNEY"; color: "#9b9b9b"; font.pixelSize: 11; font.letterSpacing: 1.1 }
        Text {
            text: root.complete ? "First Journey complete" : root.steps[root.currentStep].title
            color: "#f3f3f3"
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Text {
            text: root.complete ? "Replay these five local steps whenever you want." : root.steps[root.currentStep].body
            color: "#c5c5c5"
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: 14
        }
        Row {
            spacing: 10
            Basic.Button { text: root.complete ? "Replay" : "Continue"; activeFocusOnTab: true; onClicked: root.complete ? root.replay() : root.completeCurrent() }
            Basic.Button { text: "Skip"; visible: !root.complete; activeFocusOnTab: true; onClicked: root.skipCurrent() }
        }
    }
}
