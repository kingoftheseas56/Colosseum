// AccountYourColosseumPage.qml
// Native QML conversion of the locked "Your Colosseum — Monthly Portrait" mock.
// This file owns presentation only. Monthly statistics and activity are supplied by the host.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "yourColosseumPage"

    // Presentation-only surface: this page has no persisted/ephemeral draft state, so
    // "active" is not read here. It exists only so the composed host (AccountCenter.qml)
    // can gate this page's own `visible` with the same active/visible pattern the other
    // five locked pages use, instead of a bare activeSection comparison.
    property bool active: false

    property string monthName: ""
    property string monthYear: ""
    property string watchTimeText: ""
    property string pagesReadText: ""
    property string completedText: ""
    property string activeDaysText: ""
    property var highlights: []
    property var recentActivity: []

    property bool previousMonthEnabled: true
    property bool nextMonthEnabled: true

    signal previousMonthRequested()
    signal nextMonthRequested()

    readonly property bool widePortrait: scroller.width > 960
    readonly property bool compactCards: scroller.width < 528
    readonly property bool compactHeader: scroller.width < 440
    readonly property int highlightCount: Math.min(4, modelLength(highlights))

    Theme { id: theme }

    KeyboardScrollController {
        id: keyboardScroll
        flick: scroller
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        keyboardScroll.handle(event)
    }

    function modelLength(model) {
        if (!model)
            return 0
        if (model.count !== undefined)
            return Number(model.count)
        if (model.length !== undefined)
            return Number(model.length)
        return 0
    }

    function modelAt(model, index) {
        if (!model || index < 0 || index >= modelLength(model))
            return null
        if (typeof model.get === "function")
            return model.get(index)
        return model[index]
    }

    function textOrDash(value) {
        const text = value === undefined || value === null ? "" : String(value).trim()
        return text.length > 0 ? text : "—"
    }

    function itemText(item, key) {
        if (!item || item[key] === undefined || item[key] === null)
            return ""
        return String(item[key])
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: pageColumn.height + 58
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ScrollBar.vertical: ScrollBar {
            policy: scroller.contentHeight > scroller.height
                ? ScrollBar.AsNeeded
                : ScrollBar.AlwaysOff
        }

        Column {
            id: pageColumn
            width: Math.min(1080, scroller.width)
            spacing: 0

            Item {
                id: pageTop
                width: parent.width
                height: root.compactHeader ? 72 : 32

                Text {
                    id: pageTitle
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: qsTr("Your Colosseum")
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }

                Row {
                    id: monthNav
                    anchors.right: parent.right
                    anchors.top: root.compactHeader ? pageTitle.bottom : parent.top
                    anchors.topMargin: root.compactHeader ? 12 : 0
                    spacing: 12

                    Button {
                        id: previousMonthButton
                        objectName: "yourColosseumPreviousMonth"
                        width: 30
                        height: 30
                        enabled: root.previousMonthEnabled
                        activeFocusOnTab: true
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.right: nextMonthButton

                        background: Rectangle {
                            radius: 9
                            color: previousMonthButton.hovered
                                ? theme.glassTint
                                : "transparent"
                            border.width: previousMonthButton.activeFocus ? 1.5 : 1
                            border.color: previousMonthButton.activeFocus
                                ? theme.gold
                                : theme.edge
                            opacity: previousMonthButton.enabled ? 1 : 0.42
                        }

                        contentItem: Text {
                            text: "‹"
                            color: previousMonthButton.hovered || previousMonthButton.activeFocus
                                ? theme.ink
                                : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 17
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: root.previousMonthRequested()
                    }

                    Text {
                        width: 112
                        height: 30
                        text: root.monthName.length > 0 && root.monthYear.length > 0
                            ? qsTr("%1 %2").arg(root.monthName).arg(root.monthYear)
                            : "—"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    Button {
                        id: nextMonthButton
                        objectName: "yourColosseumNextMonth"
                        width: 30
                        height: 30
                        enabled: root.nextMonthEnabled
                        activeFocusOnTab: true
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.left: previousMonthButton

                        background: Rectangle {
                            radius: 9
                            color: nextMonthButton.hovered
                                ? theme.glassTint
                                : "transparent"
                            border.width: nextMonthButton.activeFocus ? 1.5 : 1
                            border.color: nextMonthButton.activeFocus
                                ? theme.gold
                                : theme.edge
                            opacity: nextMonthButton.enabled ? 1 : 0.42
                        }

                        contentItem: Text {
                            text: "›"
                            color: nextMonthButton.hovered || nextMonthButton.activeFocus
                                ? theme.ink
                                : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 17
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: root.nextMonthRequested()
                    }
                }
            }

            Item { width: 1; height: 30 }

            Rectangle {
                width: parent.width
                height: 1
                color: theme.edge
            }

            Item { width: 1; height: root.compactCards ? 24 : 33 }

            Grid {
                id: portraitGrid
                objectName: "yourColosseumPortraitGrid"
                width: parent.width
                columns: root.widePortrait ? 2 : 1
                columnSpacing: root.widePortrait ? 38 : 0
                rowSpacing: root.widePortrait ? 0 : 26

                Item {
                    id: monthPortrait
                    objectName: "yourColosseumMonthPortrait"
                    width: root.widePortrait
                        ? Math.max(360, (portraitGrid.width - portraitGrid.columnSpacing) * 0.41)
                        : portraitGrid.width
                    height: root.widePortrait
                        ? Math.max(260, monthHeading.height + 28 + metricsGrid.height)
                        : monthHeading.height + 28 + metricsGrid.height

                    Column {
                        id: monthHeading
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 0

                        Text {
                            text: qsTr("Monthly portrait").toUpperCase()
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.letterSpacing: 1.32
                        }

                        Item { width: 1; height: 12 }

                        Text {
                            width: parent.width
                            text: root.textOrDash(root.monthName)
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: root.compactCards ? 48 : 66
                            font.weight: Font.DemiBold
                            font.letterSpacing: -2.2
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: root.textOrDash(root.monthYear)
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: root.compactCards ? 48 : 66
                            font.weight: Font.Light
                            font.letterSpacing: -2.2
                            elide: Text.ElideRight
                        }
                    }

                    Grid {
                        id: metricsGrid
                        objectName: "yourColosseumMetricsGrid"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        y: root.widePortrait
                            ? parent.height - height
                            : monthHeading.height + 28
                        columns: root.widePortrait
                            ? 2
                            : (root.compactCards ? 2 : 4)
                        columnSpacing: root.widePortrait ? 28 : 24
                        rowSpacing: root.compactCards ? 18 : 22

                        Repeater {
                            model: [
                                { value: root.watchTimeText, label: qsTr("Watch time") },
                                { value: root.pagesReadText, label: qsTr("Pages read") },
                                { value: root.completedText, label: qsTr("Completed") },
                                { value: root.activeDaysText, label: qsTr("Active days") }
                            ]

                            Item {
                                width: (metricsGrid.width
                                    - metricsGrid.columnSpacing * (metricsGrid.columns - 1))
                                    / metricsGrid.columns
                                height: 47

                                Text {
                                    width: parent.width
                                    text: root.textOrDash(modelData.value)
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 27
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: -0.8
                                    elide: Text.ElideRight
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    text: modelData.label
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Item {
                    id: monthShelf
                    objectName: "yourColosseumMonthShelf"
                    width: root.widePortrait
                        ? portraitGrid.width - monthPortrait.width - portraitGrid.columnSpacing
                        : portraitGrid.width
                    height: root.widePortrait
                        ? Math.max(260, shelfContent.height)
                        : 24 + shelfContent.height

                    Rectangle {
                        visible: root.widePortrait
                        width: 1
                        height: parent.height
                        anchors.left: parent.left
                        color: theme.edge
                    }

                    Rectangle {
                        visible: !root.widePortrait
                        width: parent.width
                        height: 1
                        anchors.top: parent.top
                        color: theme.edge
                    }

                    Column {
                        id: shelfContent
                        x: root.widePortrait ? 28 : 0
                        y: root.widePortrait ? 0 : 24
                        width: root.widePortrait ? parent.width - 28 : parent.width
                        spacing: 0

                        Item {
                            width: parent.width
                            height: 14

                            Text {
                                anchors.left: parent.left
                                text: root.monthName.length > 0
                                    ? qsTr("%1 highlights").arg(root.monthName)
                                    : qsTr("Highlights")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            Text {
                                anchors.right: parent.right
                                text: root.highlightCount === 1
                                    ? qsTr("1 title").toUpperCase()
                                    : qsTr("%1 titles").arg(root.highlightCount).toUpperCase()
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 10
                                font.letterSpacing: 0.8
                            }
                        }

                        Item { width: 1; height: 14 }

                        Grid {
                            id: featureGrid
                            objectName: "yourColosseumFeatureGrid"
                            width: parent.width
                            columns: root.compactCards ? 2 : 4
                            columnSpacing: 12
                            rowSpacing: root.compactCards ? 16 : 0

                            Repeater {
                                model: 4

                                Item {
                                    id: feature
                                    property var entry: root.modelAt(root.highlights, index)
                                    property string titleText: root.itemText(entry, "title")
                                    property string labelText: root.itemText(entry, "label")
                                    property string valueText: root.itemText(entry, "value")
                                    property real lineAngle: index === 0 ? -28
                                        : index === 1 ? 26
                                        : index === 2 ? -8
                                        : 40
                                    property real lineOne: index === 0 ? 0.48
                                        : index === 1 ? 0.36
                                        : index === 2 ? 0.31
                                        : 0.41
                                    property real lineTwo: index === 0 ? 0.63
                                        : index === 1 ? 0.53
                                        : index === 2 ? 0.72
                                        : 0.58

                                    width: (featureGrid.width
                                        - featureGrid.columnSpacing * (featureGrid.columns - 1))
                                        / featureGrid.columns
                                    height: featureMark.height + 36

                                    Rectangle {
                                        id: featureMark
                                        width: parent.width
                                        height: width * 1.25
                                        radius: 14
                                        clip: true
                                        color: Qt.rgba(1, 1, 1, 0.055)
                                        border.width: 1
                                        border.color: theme.edge

                                        Rectangle {
                                            x: parent.width * feature.lineOne
                                            y: -parent.height * 0.25
                                            width: 1
                                            height: parent.height * 1.5
                                            color: Qt.rgba(
                                                theme.gold.r,
                                                theme.gold.g,
                                                theme.gold.b,
                                                0.38)
                                            rotation: feature.lineAngle
                                            transformOrigin: Item.Center
                                        }

                                        Rectangle {
                                            x: parent.width * feature.lineTwo
                                            y: -parent.height * 0.25
                                            width: 1
                                            height: parent.height * 1.5
                                            color: Qt.rgba(
                                                theme.gold.r,
                                                theme.gold.g,
                                                theme.gold.b,
                                                0.16)
                                            rotation: feature.lineAngle
                                            transformOrigin: Item.Center
                                        }

                                        Text {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 13
                                            text: feature.titleText
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        y: featureMark.height + 10
                                        text: feature.labelText.toUpperCase()
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 9
                                        font.letterSpacing: 0.72
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        y: featureMark.height + 23
                                        text: feature.valueText
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 32 }

            Rectangle {
                width: parent.width
                height: 1
                color: theme.edge
            }

            Item { width: 1; height: 28 }

            Text {
                text: qsTr("Recent activity")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Item { width: 1; height: 18 }

            Column {
                id: activityColumn
                width: parent.width
                spacing: 0

                Repeater {
                    model: root.modelLength(root.recentActivity)

                    Item {
                        id: activityRow
                        property var entry: root.modelAt(root.recentActivity, index)
                        width: activityColumn.width
                        height: 50

                        Text {
                            x: 0
                            y: 13
                            width: root.compactCards ? 72 : 96
                            text: root.itemText(activityRow.entry, "date")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        Item {
                            id: activityEvent
                            x: root.compactCards ? 90 : 114
                            width: parent.width - x
                            height: parent.height

                            Column {
                                anchors.left: parent.left
                                anchors.right: worldMark.left
                                anchors.rightMargin: 24
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Text {
                                    width: parent.width
                                    text: root.itemText(activityRow.entry, "title")
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: root.itemText(activityRow.entry, "meta")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                id: worldMark
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.itemText(activityRow.entry, "world").toUpperCase()
                                color: theme.gold
                                font.family: theme.ui
                                font.pixelSize: 10
                                font.letterSpacing: 0.8
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                visible: index < root.modelLength(root.recentActivity) - 1
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Qt.rgba(1, 1, 1, 0.09)
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 30 }

            Rectangle {
                width: parent.width
                height: 1
                color: theme.edge
            }
        }
    }
}
