// SourceCooldownBanner — the honest face of a rate-limited source (Spec A, 2026-07-09).
// A throttled source answers with homepage HTML; the app detects it and, instead of parsing
// garbage or blanking silently, shows this slim glass banner with a live countdown and
// auto-fires retry() when the clock lapses. Set retryInMs from a verb's meta.retryInMs
// (0 = hidden). Cached data keeps rendering behind it. Sole consumer: LocgPublisherPage.
import QtQuick

Item {
    id: banner
    property int retryInMs: 0
    property string sourceName: "Source"
    signal retry()

    visible: retryInMs > 0
    height: visible ? 46 : 0
    property int _remaining: retryInMs
    onRetryInMsChanged: _remaining = retryInMs

    Timer {
        interval: 1000; running: banner.visible; repeat: true
        onTriggered: {
            banner._remaining = Math.max(0, banner._remaining - 1000)
            if (banner._remaining <= 0) banner.retry()
        }
    }

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 0
        radius: 10
        color: Qt.rgba(0.04, 0.05, 0.07, 0.9)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)

        Row {
            anchors.centerIn: parent
            spacing: 10
            // a small gold dot as the "cooling" tell (no emoji — house law)
            Rectangle {
                width: 7; height: 7; radius: 3.5; color: theme.gold
                anchors.verticalCenter: parent.verticalCenter
                opacity: 0.5 + 0.5 * pulse.value
                Timer { id: pulseTimer; interval: 700; running: banner.visible; repeat: true
                    onTriggered: pulse.value = pulse.value > 0 ? 0 : 1 }
                QtObject { id: pulse; property real value: 1 }
                Behavior on opacity { NumberAnimation { duration: 700 } }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: banner.sourceName + " is cooling down — retrying in "
                      + Math.floor(banner._remaining / 60000) + ":"
                      + ("0" + Math.floor((banner._remaining % 60000) / 1000)).slice(-2)
                color: theme.inkDim
                font.family: theme.hud
                font.pixelSize: 13
            }
        }
    }
}
