pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "HistoryStatsModel.js" as HistoryModel

Item {
    id: root
    objectName: "historyHighlightsStatsPage"

    property Item backdrop: null
    property var activityStore: typeof ProfileActivity !== "undefined" ? ProfileActivity : null
    property var historyStore: typeof ProfileHistory !== "undefined" ? ProfileHistory : null
    property var trackerModel: typeof TrackerSyncCenter !== "undefined" ? TrackerSyncCenter : null
    property bool reducedMotion: false
    property string activeSection: "history"
    property string historyWorld: "all"
    property string statsWorld: "all"
    property string statsCategory: "overview"
    property string selectedMonthKey: currentMonthKey

    readonly property string currentMonthKey: HistoryModel.currentMonthKey()
    readonly property string earliestMonthKey: {
        if (!activityStore)
            return ""
        void activityStore.revision
        return String(activityStore.earliestActivityMonth() || "")
    }
    readonly property var monthKeys: HistoryModel.monthKeys(earliestMonthKey, currentMonthKey)
    readonly property var monthChoices: {
        var result = []
        for (var i = monthKeys.length - 1; i >= 0; --i)
            result.push({ value: monthKeys[i], label: HistoryModel.monthLabel(monthKeys[i]) })
        return result
    }
    readonly property var projections: HistoryModel.collectProjections(
        activityStore, earliestMonthKey, currentMonthKey)
    readonly property var selectedProjection: HistoryModel.projectionForMonth(
        projections, selectedMonthKey)
    readonly property var trackerDeliveryRows: {
        if (!trackerModel)
            return []
        void trackerModel.revision
        return trackerModel.historyDeliveryRows()
    }
    readonly property var historyModel: HistoryModel.historyRows(
        projections, historyWorld, trackerDeliveryRows)
    readonly property var highlightModel: HistoryModel.highlightCards(selectedProjection)
    readonly property var statsModel: HistoryModel.aggregateStats(
        projections, statsWorld, statsCategory)
    readonly property int pageInset: Math.min(theme.margin, Math.max(24, width * 0.045))
    readonly property int taskbarClearance: 92

    signal backRequested()
    signal syncCenterRequested()

    Theme { id: theme }

    function takeKeyboardFocus() {
        Qt.callLater(function() {
            if (historyTab.visible)
                historyTab.forceActiveFocus(Qt.TabFocusReason)
        })
    }

    function selectSection(section) {
        activeSection = section
        if (section === "history") historyTab.forceActiveFocus(Qt.TabFocusReason)
        else if (section === "highlights") highlightsTab.forceActiveFocus(Qt.TabFocusReason)
        else statsTab.forceActiveFocus(Qt.TabFocusReason)
    }

    function worldLabel(value) {
        if (value === "theatre") return "Theatre"
        if (value === "tankoban") return "Tankoban"
        if (value === "biblio") return "Biblio"
        return "All worlds"
    }

    function categoryLabel(value) {
        if (value === "time") return "Most time spent"
        if (value === "watch") return "Most watched"
        if (value === "listen") return "Most listened"
        if (value === "pages") return "Most pages read"
        if (value === "completed") return "Most completed"
        if (value === "sessions") return "Most sessions"
        if (value === "days") return "Most active days"
        return "Overview"
    }

    function providerInitial(name) {
        var value = String(name || "").trim()
        return value.length > 0 ? value.charAt(0).toUpperCase() : "T"
    }

    component FrostPanel: Rectangle {
        radius: 15
        color: Qt.rgba(0.09, 0.10, 0.13, 0.74)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.16)
    }

    component SectionTab: Rectangle {
        id: sectionControl
        required property string section
        required property string label
        property alias action: tabAction
        width: 132
        height: 38
        radius: 19
        color: root.activeSection === section
               ? Qt.rgba(1, 1, 1, 0.17)
               : (tabAction.interactionActive ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
        border.width: tabAction.activeFocus ? 1 : 0
        border.color: theme.gold
        Text {
            anchors.centerIn: parent
            text: sectionControl.label
            color: root.activeSection === sectionControl.section ? theme.ink : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
            font.weight: root.activeSection === sectionControl.section ? Font.DemiBold : Font.Medium
        }
        KeyboardAction {
            id: tabAction
            anchors.fill: parent
            accessibleName: sectionControl.label
            focusRadius: 19
            spaceActivates: true
            onTriggered: root.selectSection(sectionControl.section)
        }
    }

    component HouseCombo: ComboBox {
        id: combo
        implicitWidth: 168
        implicitHeight: 38
        textRole: "label"
        valueRole: "value"
        font.family: theme.ui
        font.pixelSize: 12
        contentItem: Text {
            leftPadding: 15
            rightPadding: 30
            text: combo.displayText
            color: theme.ink
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 19
            color: combo.down || combo.hovered ? Qt.rgba(1, 1, 1, 0.14)
                                                : Qt.rgba(1, 1, 1, 0.075)
            border.width: combo.activeFocus ? 1.5 : 1
            border.color: combo.activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.18)
        }
        indicator: Text {
            x: combo.width - width - 14
            y: Math.round((combo.height - height) / 2) - 1
            text: "⌄"
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 16
        }
        popup: Popup {
            y: combo.height + 6
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight + 12, 300)
            padding: 6
            background: Rectangle {
                radius: 12
                color: "#e8171920"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.20)
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
        }
        delegate: ItemDelegate {
            id: optionDelegate
            required property var modelData
            required property int index
            width: combo.width - 12
            height: 36
            highlighted: combo.highlightedIndex === optionDelegate.index
            contentItem: Text {
                text: optionDelegate.modelData.label
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 8
                color: optionDelegate.highlighted ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            }
        }
    }

    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: root.backdrop
        live: true
        hideSource: false
        visible: root.backdrop !== null
    }
    Rectangle {
        anchors.fill: parent
        color: root.backdrop ? Qt.rgba(0.015, 0.02, 0.03, 0.84) : "#05060a"
    }

    Item {
        id: pageHeader
        x: root.pageInset
        y: 24
        width: root.width - root.pageInset * 2
        height: 55

        Rectangle {
            id: backButton
            objectName: "historyStatsBackButton"
            width: 40
            height: 40
            radius: 20
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: backAction.interactionActive ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07)
            border.width: backAction.activeFocus ? 1.5 : 1
            border.color: backAction.activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.16)
            Text {
                anchors.centerIn: parent
                text: "‹"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 28
                anchors.verticalCenterOffset: -2
            }
            KeyboardAction {
                id: backAction
                anchors.fill: parent
                accessibleName: "Back"
                focusRadius: 20
                onTriggered: root.backRequested()
            }
        }

        Column {
            anchors.left: backButton.right
            anchors.leftMargin: 15
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: "Your Colosseum"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 24
            }
            Text {
                text: "The moments and measures Colosseum witnessed on this profile."
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 11
            }
        }
    }

    FrostPanel {
        id: sectionRail
        anchors.horizontalCenter: parent.horizontalCenter
        y: 92
        width: 408
        height: 50
        radius: 25
        color: Qt.rgba(0.08, 0.09, 0.12, 0.76)
        Row {
            anchors.centerIn: parent
            spacing: 2
            SectionTab {
                id: historyTab
                objectName: "historySectionTab"
                section: "history"
                label: "History"
                action.KeyNavigation.right: highlightsTab.action
            }
            SectionTab {
                id: highlightsTab
                objectName: "highlightsSectionTab"
                section: "highlights"
                label: "Highlights"
                action.KeyNavigation.left: historyTab.action
                action.KeyNavigation.right: statsTab.action
            }
            SectionTab {
                id: statsTab
                objectName: "statsSectionTab"
                section: "stats"
                label: "Stats"
                action.KeyNavigation.left: highlightsTab.action
            }
        }
    }

    Loader {
        id: sectionLoader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: sectionRail.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.taskbarClearance
        sourceComponent: root.activeSection === "history" ? historyComponent
                         : (root.activeSection === "highlights" ? highlightsComponent : statsComponent)
    }

    Component {
        id: historyComponent
        Flickable {
            id: historyScroll
            objectName: "historyPageScroll"
            contentWidth: width
            contentHeight: historyBody.implicitHeight + 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: historyScroll }

            Column {
                id: historyBody
                x: root.pageInset
                width: historyScroll.width - root.pageInset * 2
                spacing: 14

                RowLayout {
                    width: parent.width
                    spacing: 16
                    Column {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "History"
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 33
                        }
                        Text {
                            text: "Everything watched, read, or listened to in Colosseum appears here."
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                    HouseCombo {
                        id: historyWorldFilter
                        objectName: "historyWorldFilter"
                        model: [
                            { value: "all", label: "All worlds" },
                            { value: "theatre", label: "Theatre" },
                            { value: "tankoban", label: "Tankoban" },
                            { value: "biblio", label: "Biblio" },
                            { value: "vault", label: "Vault" }
                        ]
                        currentIndex: Math.max(0, model.findIndex(function(row) { return row.value === root.historyWorld }))
                        onActivated: (index) => root.historyWorld = model[index].value
                    }
                }

                Text {
                    visible: root.historyModel.length > 0
                    text: "RECENT"
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.4
                    topPadding: 10
                }

                Repeater {
                    model: root.historyModel
                    delegate: FrostPanel {
                        id: historyRowDelegate
                        required property var modelData
                        required property int index
                        objectName: "historyActivityRow_" + index
                        width: historyBody.width
                        height: 112
                        color: Qt.rgba(1, 1, 1, rowHover.hovered ? 0.085 : 0.055)

                        HoverHandler { id: rowHover }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 142
                                Layout.fillHeight: true
                                radius: 10
                                clip: true
                                color: Qt.rgba(1, 1, 1, 0.06)
                                Image {
                                    id: historyArt
                                    anchors.fill: parent
                                    source: historyRowDelegate.modelData.cover
                                    visible: status === Image.Ready
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: historyArt.status !== Image.Ready
                                    text: historyRowDelegate.modelData.worldLabel.charAt(0)
                                    color: theme.inkDimmer
                                    font.family: theme.display
                                    font.pixelSize: 30
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 5
                                Text {
                                    text: historyRowDelegate.modelData.worldLabel.toUpperCase()
                                    color: theme.gold
                                    font.family: theme.ui
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 1.2
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: historyRowDelegate.modelData.title
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: historyRowDelegate.modelData.detail
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: historyRowDelegate.modelData.localDate
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                }
                            }
                            RowLayout {
                                spacing: 7
                                Repeater {
                                    model: historyRowDelegate.modelData.trackerReceipts
                                    delegate: Rectangle {
                                        required property var modelData
                                        Layout.preferredWidth: 27
                                        Layout.preferredHeight: 27
                                        radius: 13.5
                                        color: modelData.state === "confirmed"
                                               ? Qt.rgba(0.40, 0.75, 0.55, 0.18)
                                               : modelData.state === "needs_attention"
                                                 || modelData.state === "failed"
                                                 ? Qt.rgba(0.88, 0.37, 0.34, 0.18)
                                                 : Qt.rgba(0.81, 0.65, 0.27, 0.18)
                                        border.width: 1
                                        border.color: modelData.state === "confirmed"
                                                      ? Qt.rgba(0.40, 0.75, 0.55, 0.70)
                                                      : modelData.state === "needs_attention"
                                                        || modelData.state === "failed"
                                                        ? Qt.rgba(0.88, 0.37, 0.34, 0.70)
                                                        : Qt.rgba(0.81, 0.65, 0.27, 0.70)
                                        Accessible.name: modelData.providerName + ": " + modelData.state
                                        Text {
                                            anchors.centerIn: parent
                                            text: root.providerInitial(parent.modelData.providerName)
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                            font.bold: true
                                        }
                                    }
                                }
                                Rectangle {
                                    id: receiptStatusBadge
                                    Layout.preferredWidth: localLabel.implicitWidth + 20
                                    Layout.preferredHeight: 28
                                    radius: 14
                                    color: Qt.rgba(1, 1, 1, 0.06)
                                    border.width: receiptStatusAction.activeFocus ? 2 : 1
                                    border.color: receiptStatusAction.activeFocus
                                                  ? theme.gold : Qt.rgba(1, 1, 1, 0.12)
                                    Text {
                                        id: localLabel
                                        anchors.centerIn: parent
                                        text: historyRowDelegate.modelData.syncLabel
                                        color: historyRowDelegate.modelData.syncLabel === "Needs attention"
                                               ? Qt.rgba(1.0, 0.55, 0.52, 1.0) : theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                    }
                                    KeyboardAction {
                                        id: receiptStatusAction
                                        objectName: historyRowDelegate.modelData.syncLabel === "Needs attention"
                                                    ? "historyDiagnoseSyncButton" : ""
                                        anchors.fill: parent
                                        enabled: historyRowDelegate.modelData.syncLabel === "Needs attention"
                                        accessibleName: "Open tracker diagnostics for "
                                                        + historyRowDelegate.modelData.title
                                        focusRadius: receiptStatusBadge.radius
                                        spaceActivates: true
                                        onTriggered: root.syncCenterRequested()
                                    }
                                }
                            }
                        }
                    }
                }

                Column {
                    visible: root.historyModel.length === 0
                    width: parent.width
                    topPadding: 86
                    spacing: 8
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "No activity yet"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 27
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.activityStore
                              ? "Play, read, or listen in Colosseum and this history will begin here."
                              : "Activity is unavailable for the current profile."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    Component {
        id: highlightsComponent
        Flickable {
            id: highlightsScroll
            objectName: "highlightsPageScroll"
            contentWidth: width
            contentHeight: highlightsBody.implicitHeight + 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: highlightsScroll }

            Column {
                id: highlightsBody
                x: root.pageInset
                width: highlightsScroll.width - root.pageInset * 2
                spacing: 26

                RowLayout {
                    width: parent.width
                    spacing: 16
                    Column {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "Highlights"
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 33
                        }
                        Text {
                            text: "A portrait of the month, drawn only from activity Colosseum recorded."
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                    HouseCombo {
                        id: monthFilter
                        objectName: "highlightsMonthFilter"
                        implicitWidth: 178
                        model: root.monthChoices
                        currentIndex: Math.max(0, model.findIndex(function(row) { return row.value === root.selectedMonthKey }))
                        onActivated: (index) => root.selectedMonthKey = model[index].value
                    }
                }

                GridLayout {
                    width: parent.width
                    columns: width >= 920 ? 3 : 1
                    columnSpacing: 28
                    rowSpacing: 24

                    Item {
                        Layout.columnSpan: highlightsBody.width >= 920 ? 1 : 1
                        Layout.preferredWidth: highlightsBody.width >= 920 ? highlightsBody.width * 0.34 : highlightsBody.width
                        Layout.fillWidth: highlightsBody.width < 920
                        Layout.preferredHeight: 390

                        Column {
                            anchors.fill: parent
                            spacing: 6
                            Text {
                                text: "MONTHLY PORTRAIT"
                                color: theme.gold
                                font.family: theme.ui
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.6
                            }
                            Text {
                                text: HistoryModel.monthLabel(root.selectedMonthKey).split(" ")[0]
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: Math.min(66, root.width * 0.055)
                                font.weight: Font.Medium
                            }
                            Text {
                                text: root.selectedMonthKey.slice(0, 4)
                                color: theme.inkDim
                                font.family: theme.display
                                font.pixelSize: 25
                            }
                            Grid {
                                id: portraitMetrics
                                width: parent.width
                                columns: 2
                                spacing: 18
                                topPadding: 34
                                Repeater {
                                    model: [
                                        { value: HistoryModel.formatDuration(root.selectedProjection.watchSeconds), label: "Watch time" },
                                        { value: HistoryModel.formatCount(root.selectedProjection.pagesRead), label: "Pages read" },
                                        { value: HistoryModel.formatDuration(root.selectedProjection.listenSeconds), label: "Listened" },
                                        { value: HistoryModel.formatCount(root.selectedProjection.activeDays), label: "Active days" }
                                    ]
                                    delegate: Column {
                                        id: portraitMetricDelegate
                                        required property var modelData
                                        width: (portraitMetrics.width - portraitMetrics.spacing) / 2
                                        spacing: 3
                                        Text {
                                            text: portraitMetricDelegate.modelData.value
                                            color: theme.ink
                                            font.family: theme.display
                                            font.pixelSize: 25
                                        }
                                        Text {
                                            text: portraitMetricDelegate.modelData.label
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        visible: highlightsBody.width >= 920
                        color: Qt.rgba(1, 1, 1, 0.16)
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(390, highlightGrid.implicitHeight + 52)
                        Column {
                            anchors.fill: parent
                            spacing: 16
                            RowLayout {
                                width: parent.width
                                Text {
                                    Layout.fillWidth: true
                                    text: HistoryModel.monthLabel(root.selectedMonthKey) + " highlights"
                                    color: theme.ink
                                    font.family: theme.display
                                    font.pixelSize: 22
                                }
                                Text {
                                    text: root.highlightModel.length + (root.highlightModel.length === 1 ? " TITLE" : " TITLES")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 9
                                    font.letterSpacing: 1.2
                                }
                            }
                            Grid {
                                id: highlightGrid
                                width: parent.width
                                columns: width >= 650 ? 4 : 2
                                spacing: 13
                                Repeater {
                                    model: root.highlightModel
                                    delegate: Column {
                                        id: highlightDelegate
                                        required property var modelData
                                        width: (highlightGrid.width - highlightGrid.spacing * (highlightGrid.columns - 1)) / highlightGrid.columns
                                        spacing: 6
                                        Rectangle {
                                            width: parent.width
                                            height: Math.max(126, width * 1.28)
                                            radius: 12
                                            clip: true
                                            color: Qt.rgba(1, 1, 1, 0.06)
                                            Image {
                                                anchors.fill: parent
                                                source: highlightDelegate.modelData.cover
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                            }
                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                height: 56
                                                gradient: Gradient {
                                                    GradientStop { position: 0; color: "transparent" }
                                                    GradientStop { position: 1; color: "#e5000000" }
                                                }
                                            }
                                            Text {
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                anchors.margins: 9
                                                text: highlightDelegate.modelData.title
                                                color: theme.ink
                                                font.family: theme.ui
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Text {
                                            text: highlightDelegate.modelData.label
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 9
                                        }
                                        Text {
                                            text: highlightDelegate.modelData.value
                                            color: theme.ink
                                            font.family: theme.display
                                            font.pixelSize: 17
                                        }
                                    }
                                }
                            }
                            Column {
                                visible: root.highlightModel.length === 0
                                width: parent.width
                                topPadding: 70
                                spacing: 8
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "No activity yet"
                                    color: theme.ink
                                    font.family: theme.display
                                    font.pixelSize: 24
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "There is no Colosseum activity for this month."
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: statsComponent
        Flickable {
            id: statsScroll
            objectName: "statsPageScroll"
            contentWidth: width
            contentHeight: statsBody.implicitHeight + 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: statsScroll }

            Column {
                id: statsBody
                x: root.pageInset
                width: statsScroll.width - root.pageInset * 2
                spacing: 20

                RowLayout {
                    width: parent.width
                    spacing: 12
                    Column {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "Stats"
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 33
                        }
                        Text {
                            text: "Lifetime activity recorded on this profile."
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                    HouseCombo {
                        id: statsWorldFilter
                        objectName: "statsWorldFilter"
                        model: [
                            { value: "all", label: "All worlds" },
                            { value: "theatre", label: "Theatre" },
                            { value: "tankoban", label: "Tankoban" },
                            { value: "biblio", label: "Biblio" },
                            { value: "vault", label: "Vault" }
                        ]
                        currentIndex: Math.max(0, model.findIndex(function(row) { return row.value === root.statsWorld }))
                        onActivated: (index) => root.statsWorld = model[index].value
                    }
                    HouseCombo {
                        id: statsCategoryFilter
                        objectName: "statsCategoryFilter"
                        implicitWidth: 184
                        model: [
                            { value: "overview", label: "Overview" },
                            { value: "time", label: "Most time spent" },
                            { value: "watch", label: "Most watched" },
                            { value: "pages", label: "Most pages read" },
                            { value: "listen", label: "Most listened" },
                            { value: "completed", label: "Most completed" },
                            { value: "sessions", label: "Most sessions" },
                            { value: "days", label: "Most active days" }
                        ]
                        currentIndex: Math.max(0, model.findIndex(function(row) { return row.value === root.statsCategory }))
                        onActivated: (index) => root.statsCategory = model[index].value
                    }
                }

                FrostPanel {
                    width: parent.width
                    height: 92
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: root.statsModel.summary
                            delegate: Item {
                                id: summaryDelegate
                                required property var modelData
                                required property int index
                                width: parent.width / 5
                                height: parent.height
                                Rectangle {
                                    visible: summaryDelegate.index > 0
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 1
                                    height: parent.height - 28
                                    color: Qt.rgba(1, 1, 1, 0.12)
                                }
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: summaryDelegate.modelData.value
                                        color: theme.ink
                                        font.family: theme.display
                                        font.pixelSize: 23
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: summaryDelegate.modelData.label
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    width: parent.width
                    Text {
                        Layout.fillWidth: true
                        text: root.categoryLabel(root.statsCategory)
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 21
                    }
                    Text {
                        text: root.worldLabel(root.statsWorld).toUpperCase() + " · LIFETIME"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 9
                        font.letterSpacing: 1.2
                    }
                }

                FrostPanel {
                    width: parent.width
                    height: Math.max(70, statsRowsColumn.implicitHeight)
                    clip: true
                    Column {
                        id: statsRowsColumn
                        width: parent.width
                        Repeater {
                            model: root.statsModel.rows
                            delegate: Item {
                                id: statsRowDelegate
                                required property var modelData
                                required property int index
                                width: statsRowsColumn.width
                                height: 58
                                Rectangle {
                                    visible: statsRowDelegate.index > 0
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    height: 1
                                    color: Qt.rgba(1, 1, 1, 0.09)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 18
                                    anchors.rightMargin: 18
                                    spacing: 16
                                    Text {
                                        Layout.preferredWidth: 24
                                        text: statsRowDelegate.index + 1
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: statsRowDelegate.modelData.title
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.preferredWidth: 120
                                        text: statsRowDelegate.modelData.value
                                        color: theme.ink
                                        font.family: theme.display
                                        font.pixelSize: 15
                                    }
                                    Text {
                                        Layout.preferredWidth: 95
                                        text: statsRowDelegate.modelData.worldLabel
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                    }
                                    Text {
                                        Layout.preferredWidth: 110
                                        text: statsRowDelegate.modelData.details
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }
                        }
                        Item {
                            visible: root.statsModel.empty
                            width: parent.width
                            height: 110
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "No activity yet"
                                    color: theme.ink
                                    font.family: theme.display
                                    font.pixelSize: 22
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Stats will appear after Colosseum records activity for this profile."
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
