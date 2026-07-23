import QtQuick
import QtQuick.Window
import Colosseum.Player2 1.0
import "."

Window {
    id: root
    width: 1280
    height: 720
    minimumWidth: 980
    minimumHeight: 600
    visible: true
    color: Theme.ink
    title: "Player 2 laboratory"

    component MetricRow: Item {
        required property string label
        required property string value
        width: parent ? parent.width : 0
        height: 42

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            color: Theme.muted
            font.family: Theme.displayFont
            font.pixelSize: 13
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: parent.value
            color: Theme.frost
            font.family: Theme.dataFont
            font.pixelSize: 14
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.hairline
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 22

        Item {
            id: stage
            width: parent.width - instrument.width - parent.spacing
            height: parent.height

            Rectangle {
                anchors.fill: parent
                color: "#02070c"
                border.width: 1
                border.color: Theme.hairline

                Player2VideoItem {
                    id: videoSurface
                    anchors.fill: parent
                    anchors.margins: 1
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 18
                    width: liveLabel.width + 24
                    height: 30
                    radius: 2
                    color: "#cc07111d"
                    border.color: Theme.signal
                    Text {
                        id: liveLabel
                        anchors.centerIn: parent
                        text: "D3D11  /  ZERO COPY"
                        color: Theme.signal
                        font.family: Theme.dataFont
                        font.pixelSize: 11
                        font.letterSpacing: 1.4
                    }
                }
            }

            Row {
                id: frameRail
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                spacing: 4
                visible: !shellToggle.checked
                Repeater {
                    model: 32
                    Rectangle {
                        required property int index
                        width: Math.max(3, (frameRail.width - 31 * frameRail.spacing) / 32)
                        height: index % 4 === 0 ? 11 : 6
                        anchors.bottom: parent.bottom
                        color: index <= (HarnessHost.presented % 32) ? Theme.signal : Theme.hairline
                        opacity: index <= (HarnessHost.presented % 32) ? 0.9 : 0.45
                    }
                }
            }

            // Parity chrome overlay — the Player 2 shell driven by the live session (Task 13).
            Player2Shell {
                id: playerShell
                anchors.fill: parent
                anchors.margins: 1
                visible: shellToggle.checked
                session: videoSurface.session
                hostServices: HarnessHost
                // The shell only requests fullscreen (typed intent); the host owns the window. In the
                // lab the host is this Window — toggle it. Production wires this to its own window.
                onFullscreenRequested: root.visibility =
                    (root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen)
            }

            // Lab toggle between the raw frame-rail instrument and the parity chrome.
            Rectangle {
                id: shellToggle
                property bool checked: true
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18
                width: toggleLabel.width + 24
                height: 30
                radius: 2
                color: "#cc07111d"
                border.color: checked ? "#f0c44a" : Theme.hairline
                z: 10
                Text {
                    id: toggleLabel
                    anchors.centerIn: parent
                    text: shellToggle.checked ? "CHROME" : "INSTRUMENT"
                    color: shellToggle.checked ? "#f0c44a" : Theme.muted
                    font.family: Theme.dataFont
                    font.pixelSize: 11
                    font.letterSpacing: 1.4
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: shellToggle.checked = !shellToggle.checked
                }
            }
        }

        Rectangle {
            id: instrument
            width: 318
            height: parent.height
            color: Theme.panel
            border.width: 1
            border.color: Theme.hairline

            Column {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 0

                Text {
                    text: "PLAYBACK INSTRUMENT"
                    color: Theme.signal
                    font.family: Theme.dataFont
                    font.pixelSize: 11
                    font.letterSpacing: 1.2
                }
                Text {
                    width: parent.width
                    topPadding: 8
                    bottomPadding: 18
                    text: HarnessHost.status
                    color: Theme.frost
                    wrapMode: Text.WordWrap
                    font.family: Theme.displayFont
                    font.pixelSize: 25
                    font.weight: Font.DemiBold
                }

                MetricRow { label: "Source"; value: HarnessHost.source }
                MetricRow { label: "Decode path"; value: HarnessHost.decodePath }
                MetricRow { label: "Session"; value: HarnessHost.sessionState }
                MetricRow { label: "Duration"; value: HarnessHost.duration.toFixed(2) + " s" }
                MetricRow { label: "Tracks"; value: String(HarnessHost.trackCount) }
                MetricRow { label: "Generated"; value: String(HarnessHost.generated) }
                MetricRow { label: "Presented"; value: String(HarnessHost.presented) }
                MetricRow { label: "Dropped"; value: String(HarnessHost.dropped) }
                MetricRow { label: "Late"; value: String(HarnessHost.late) }
                MetricRow { label: "CPU transfers"; value: String(HarnessHost.cpuTransfers) }
                MetricRow { label: "Device errors"; value: String(HarnessHost.deviceErrors) }

                Text {
                    width: parent.width
                    topPadding: 20
                    text: HarnessHost.adapter || "Resolving graphics adapter…"
                    color: HarnessHost.adapterMatch ? Theme.signal : Theme.amber
                    wrapMode: Text.WordWrap
                    font.family: Theme.dataFont
                    font.pixelSize: 12
                }
            }
        }
    }

    Component.onCompleted: HarnessHost.attachVideoItem(videoSurface)
}
