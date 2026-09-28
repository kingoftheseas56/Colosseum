pragma ComponentBehavior: Bound
import QtQuick
import ".."

Item {
    id: stats
    property var episode: ({})
    implicitHeight: statFlow.implicitHeight
    HarborTheme { id: theme }

    function progressText() {
        var ratio = Number(stats.episode.progress || 0)
        if (stats.episode.watched || ratio >= 0.85) return "Watched"
        if (ratio > 0.01) return Math.round(ratio * 100) + "% watched"
        return "Not started"
    }

    Flow {
        id: statFlow
        width: parent.width
        spacing: 18

        Repeater {
            model: [
                { label: "IMDb", value: String(stats.episode.rating || "—") },
                { label: "Votes", value: String(stats.episode.votes || "—") },
                { label: "Runtime", value: String(stats.episode.runtime || "—") },
                { label: "Aired", value: String(stats.episode.airDate || "—") },
                { label: "Progress", value: stats.progressText() }
            ]

            delegate: Row {
                required property var modelData
                spacing: 6
                height: 16

                Text {
                    text: modelData.label.toUpperCase()
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
                Text {
                    text: modelData.value
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
