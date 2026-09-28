pragma ComponentBehavior: Bound
import QtQuick
import ".."

Item {
    id: root
    property var groups: []
    implicitHeight: content.implicitHeight
    height: implicitHeight

    HarborTheme { id: theme }

    Column {
        id: content
        width: parent.width
        spacing: 40

        Rectangle {
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }

        Text {
            text: "Awards & Recognition"
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 24
            font.weight: Font.Medium
        }

        Repeater {
            model: root.groups

            delegate: Item {
                id: group
                required property var modelData
                width: content.width
                height: Math.max(112, entries.implicitHeight)

                Row {
                    anchors.fill: parent
                    spacing: 56

                    Item {
                        width: 240
                        height: parent.height

                        Row {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            spacing: 18

                            Item {
                                width: 78
                                height: 74

                                Text {
                                    anchors.centerIn: parent
                                    text: "‹   ›"
                                    color: "#D4AF37"
                                    font.family: theme.display
                                    font.pixelSize: 50
                                }

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 24
                                    height: 32
                                    radius: 4
                                    color: "transparent"
                                    border.width: 2
                                    border.color: "#D4AF37"
                                }
                            }

                            Column {
                                width: 140
                                spacing: 6
                                anchors.verticalCenter: parent.verticalCenter

                                Text {
                                    width: parent.width
                                    text: group.modelData.title || "Awards"
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 18
                                    font.weight: Font.Medium
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    width: parent.width
                                    text: (group.modelData.wins || 0) + ((group.modelData.wins || 0) === 1 ? " WIN" : " WINS")
                                          + ((group.modelData.nominations || 0) > 0
                                             ? "  ·  " + group.modelData.nominations + " NOMINATIONS" : "")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 1.3
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    visible: String(group.modelData.years || "").length > 0
                                    text: group.modelData.years || ""
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    Grid {
                        id: entries
                        width: Math.max(0, parent.width - 296)
                        columns: width >= 760 ? 2 : 1
                        columnSpacing: 40
                        rowSpacing: 0

                        Repeater {
                            model: group.modelData.entries || []

                            delegate: Item {
                                id: entry
                                required property var modelData
                                width: (entries.width - (entries.columns - 1) * entries.columnSpacing) / entries.columns
                                height: 61

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: Qt.rgba(1, 1, 1, 0.06)
                                }

                                Row {
                                    anchors.fill: parent
                                    anchors.topMargin: 10
                                    anchors.bottomMargin: 10
                                    spacing: 16

                                    Text {
                                        width: 44
                                        text: entry.modelData.year || "–"
                                        color: entry.modelData.won === false ? theme.inkDimmer : theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }

                                    Column {
                                        width: Math.max(0, parent.width - 60)
                                        spacing: 3

                                        Text {
                                            width: parent.width
                                            text: entry.modelData.category || ""
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            visible: String(entry.modelData.recipient || "").length > 0
                                            width: parent.width
                                            text: entry.modelData.recipient || ""
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
