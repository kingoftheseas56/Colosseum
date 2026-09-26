import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "ratingsReviewsReviewWall"
    width: parent ? parent.width : 1000
    height: reviews.length ? wall.implicitHeight : 92

    property var reviews: []
    property string emptyMessage: "No provider reviews available for this title."
    signal reviewRequested(var row, var invokingItem)

    Text {
        anchors.centerIn: parent
        visible: root.reviews.length === 0
        text: root.emptyMessage
        color: "#737c87"
        font.pixelSize: 12
    }

    GridLayout {
        id: wall
        visible: root.reviews.length > 0
        width: parent.width
        columns: width > 980 ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12

        Repeater {
            model: root.reviews
            delegate: Rectangle {
                id: tile
                required property var modelData
                required property int index
                objectName: "rrReview_" + String(modelData.providerId || "")
                Layout.fillWidth: true
                Layout.preferredHeight: 188
                radius: 16
                color: "#11161d"
                border.width: activeFocus ? 2 : 1
                border.color: activeFocus ? "#d8b56c" : "#2d333d"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Open " + String(modelData.providerName || modelData.providerId || "")
                    + " review"
                property bool spoilerRevealed: false

                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10
                    Text {
                        text: String(tile.modelData.providerName || tile.modelData.providerId || "")
                        color: "#9da5af"
                        font.pixelSize: 10
                    }
                    Text {
                        width: parent.width
                        text: String(tile.modelData.headline || "")
                        color: "#f5f3ee"
                        font.family: "Georgia"
                        font.pixelSize: 22
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                    }
                    Text {
                        width: parent.width
                        text: tile.modelData.spoiler && !tile.spoilerRevealed
                              ? "Spoiler review" : String(tile.modelData.excerpt || "")
                        color: tile.modelData.spoiler && !tile.spoilerRevealed
                               ? "#d8b56c" : "#c2c7ce"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        width: parent.width
                        Text {
                            Layout.fillWidth: true
                            text: String(tile.modelData.reviewer || tile.modelData.sourceLabel || "")
                            color: "#818a96"
                            font.pixelSize: 10
                            textFormat: Text.PlainText
                        }
                        Text {
                            text: String(tile.modelData.scoreDisplay || "")
                            color: "#e9e4d8"
                            font.pixelSize: 12
                        }
                    }
                }

                TapHandler {
                    id: tileTap
                    onTapped: {
                        if (tile.modelData.spoiler && !tile.spoilerRevealed) {
                            tile.spoilerRevealed = true
                            return
                        }
                        root.reviewRequested(tile.modelData, tile)
                    }
                }
                Keys.onPressed: function(event) {
                    if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter
                            && event.key !== Qt.Key_Space)
                        return
                    if (tile.modelData.spoiler && !tile.spoilerRevealed)
                        tile.spoilerRevealed = true
                    else
                        root.reviewRequested(tile.modelData, tile)
                    event.accepted = true
                }

                Item {
                    objectName: "rrReviewOpen_" + String(tile.modelData.providerId || "")
                    visible: false
                    width: 0
                    height: 0
                }
            }
        }
    }
}
