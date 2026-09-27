import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "../../../../qml/account"

Window {
    id: win

    readonly property var captureArgs: Qt.application.arguments
    readonly property int requestedWidth: {
        const value = Number(captureArgs[captureArgs.length - 3])
        return value > 0 ? value : 1920
    }
    readonly property int requestedHeight: {
        const value = Number(captureArgs[captureArgs.length - 2])
        return value > 0 ? value : 1080
    }
    readonly property string outputPath:
        captureArgs.length >= 4 ? captureArgs[captureArgs.length - 1] : "account-parts-source.png"

    width: requestedWidth
    height: requestedHeight
    visible: true
    flags: Qt.FramelessWindowHint
    color: "#0d0c09"

    FontLoader { source: "../../../../assets/fonts/Fraunces-Regular.ttf" }

    AccountPageFrame {
        anchors.fill: parent
        eyebrow: "COLOSSEUM · ACCOUNT"
        headline: "Shared account foundation."
        detail: "One set of account controls keeps sign-in, recovery, security, and profile flows visually consistent."
        panelWidth: 560

        AccountPanelHeader {
            kicker: "ACCOUNT CENTER"
            title: "Shared account parts"
            copy: "One source of truth for fields, buttons, and choices."
        }

        Item { width: 1; height: 24 }

        AccountField {
            width: parent.width
            label: "Password"
            hint: "Use the reveal control to check what you typed."
            placeholderText: "Your password"
            password: true
            maximumLength: 512
            inputMethodHints: Qt.ImhNoPredictiveText
        }

        Item { width: 1; height: 20 }

        AccountChoice {
            width: parent.width
            title: "Use this device"
            detail: "Approve this device and continue."
        }
        Item { width: 1; height: 20 }

        Row {
            spacing: 16

            AccountButton {
                text: "Continue"
                variant: "primary"
            }

            AccountButton {
                text: "Not now"
            }
        }

        Item { width: 1; height: 12 }

        AccountButton {
            text: "Need help?"
            variant: "link"
        }
    }

    Timer {
        interval: 900
        running: true
        repeat: false
        onTriggered: {
            win.contentItem.grabToImage(function(result) {
                result.saveToFile(win.outputPath)
                Qt.quit()
            }, Qt.size(win.width, win.height))
        }
    }
}
