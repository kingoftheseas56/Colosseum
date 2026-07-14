// MangaTankobanSourceCard — one manual source card inside a volume's inline chooser.
//
// Bound to `modelData`, a single source map from TankobanVolumes.sourcesReady:
//   * Nyaa   (kind == "nyaa")        — the UNMODIFIED release title plus INLINE
//     evidence (uploader · trust tier · size · seeders) and parsed coverage chips
//     (DIGITAL / SINGLE VOLUME / VOLS a-b). The chips EXPLAIN coverage; they never
//     claim the spreads inside are intact.
//   * WeebCentral (kind == "weebcentral") — the quieter "Build from chapters"
//     fallback; disabled (with modelData.reason) when it can't build.
//
// No colour, no emoji: gray/black/white with gold reserved for hover/active only.
// Tapping an enabled card emits chosen(); the library routes it to the right call.
import QtQuick

Item {
    id: card
    property var modelData: ({})
    property string volumeId: ""
    signal chosen()

    readonly property bool isWeeb: (modelData && modelData.kind === "weebcentral")
    // Nyaa is enabled when it has a real infoHash (service already sets `enabled`);
    // WeebCentral carries its own enabled flag + reason.
    readonly property bool cardEnabled: modelData ? (modelData.enabled !== false) : false

    width: parent ? parent.width : 520
    implicitHeight: shell.implicitHeight + 10
    height: implicitHeight

    Theme { id: theme }

    function fmtSize(bytes) {
        var n = Number(bytes) || 0
        if (n <= 0) return ""
        var mb = n / 1048576
        if (mb >= 1024) return (mb / 1024).toFixed(1) + " GB"
        return (mb >= 100 ? mb.toFixed(0) : mb.toFixed(1)) + " MB"
    }
    function tierLabel(t) {
        var n = Number(t)
        if (n === 1) return "Trusted uploader"
        if (n === 2) return "Known uploader"
        return "Community"
    }
    // Coverage chips explain, never overclaim. digital + span are independent facts.
    function coverageChips() {
        var out = []
        if (!card.modelData) return out
        if (card.modelData.digital) out.push("Digital")
        if (card.modelData.standalone) out.push("Single volume")
        else if (card.modelData.coverageLo && card.modelData.coverageHi)
            out.push("Vols " + card.modelData.coverageLo + "–" + card.modelData.coverageHi)
        return out
    }

    Rectangle {
        id: shell
        anchors.left: parent.left; anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        implicitHeight: body.height + 22
        height: implicitHeight
        radius: 10
        color: cardMa.containsMouse && card.cardEnabled ? theme.glassHi : theme.glassTint
        border.width: 1
        border.color: cardMa.containsMouse && card.cardEnabled ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
        opacity: card.cardEnabled ? 1.0 : 0.55

        Column {
            id: body
            anchors.left: parent.left; anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: 7

            // release title (unmodified) — or the WeebCentral fallback label
            Text {
                width: parent.width
                text: card.isWeeb
                    ? "Build from chapters"
                    : (card.modelData && card.modelData.releaseTitle ? card.modelData.releaseTitle : "")
                color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                font.weight: Font.DemiBold; elide: Text.ElideRight
            }

            // Nyaa evidence — INLINE, no pills: uploader · trust · size · seeders
            Row {
                visible: !card.isWeeb
                spacing: 8
                Text {
                    visible: text.length > 0
                    text: card.modelData ? String(card.modelData.uploader || "") : ""
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text { text: "·"; color: theme.inkDimmer
                    visible: (card.modelData && card.modelData.uploader) ? true : false
                    anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: card.tierLabel(card.modelData ? card.modelData.tier : 99)
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text { text: "·"; color: theme.inkDimmer
                    visible: card.fmtSize(card.modelData ? card.modelData.sizeBytes : 0).length > 0
                    anchors.verticalCenter: parent.verticalCenter }
                Text {
                    visible: text.length > 0
                    text: card.fmtSize(card.modelData ? card.modelData.sizeBytes : 0)
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text { text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: (card.modelData ? Number(card.modelData.seeders || 0) : 0) + " seeders"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // coverage chips (Nyaa only)
            Row {
                visible: !card.isWeeb && card.coverageChips().length > 0
                spacing: 7
                Repeater {
                    model: card.coverageChips()
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        implicitWidth: chipTx.implicitWidth + 16; height: 20; radius: 4
                        color: "transparent"; border.width: 1; border.color: theme.edge
                        Text {
                            id: chipTx; anchors.centerIn: parent; text: chip.modelData
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10
                            font.letterSpacing: 1; font.capitalization: Font.AllUppercase
                        }
                    }
                }
            }

            // WeebCentral explainer / disabled reason
            Text {
                visible: card.isWeeb
                width: parent.width
                text: card.cardEnabled
                    ? ("Compiles this volume from " + (card.modelData && card.modelData.chapterCount ? card.modelData.chapterCount : 0) + " WeebCentral chapters.")
                    : (card.modelData && card.modelData.reason ? card.modelData.reason : "Unavailable.")
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        MouseArea {
            id: cardMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: card.cardEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (card.cardEnabled) card.chosen()
        }
    }
}
