// AccountDataPrivacyPage.qml
// Native QML conversion of the locked Account Centre Data & privacy mock.
// Privacy policy values are authoritative host inputs. This page emits intent only.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "accountDataPrivacyPage"

    property bool active: false

    // Authoritative presentation state. Product defaults are On, but live
    // adoption must bind these to the privacy backend rather than persisting
    // them inside this component.
    property bool rememberSearchHistory: true
    property bool keepActivityHistory: true
    property bool syncActivityHistory: true

    // Host-driven in-flight/error state. None of these implies success.
    property bool rememberSearchHistoryBusy: false
    property bool keepActivityHistoryBusy: false
    property bool syncActivityHistoryBusy: false
    property bool clearSearchHistoryBusy: false
    property bool clearActivityHistoryBusy: false
    property bool dataExportBusy: false
    property bool accountDeletionFlowBusy: false
    property string errorMessage: ""

    // Ephemeral presentation state only.
    property bool searchClearConfirmationOpen: false
    property bool activityClearConfirmationOpen: false
    property bool deleteConfirmationOpen: false

    readonly property bool compactRows: scroller.width < 900
    readonly property bool compactMap: scroller.width < 900

    signal rememberSearchHistoryChangeRequested(bool enabled)
    signal keepActivityHistoryChangeRequested(bool enabled)
    signal syncActivityHistoryChangeRequested(bool enabled)
    signal clearSearchHistoryRequested()
    signal clearActivityHistoryRequested()
    signal dataExportRequested()
    signal accountDeletionFlowRequested()

    Theme { id: theme }

    readonly property color edgeSoft: Qt.rgba(1, 1, 1, 0.09)
    readonly property color danger: "#d86b66"

    function clearTransientState() {
        searchClearConfirmationOpen = false
        activityClearConfirmationOpen = false
        deleteConfirmationOpen = false
    }

    function openSearchClearConfirmation() {
        searchClearConfirmationOpen = true
        Qt.callLater(function() {
            if (root.active && root.searchClearConfirmationOpen)
                clearSearchCancel.forceActiveFocus()
        })
    }

    function closeSearchClearConfirmation() {
        searchClearConfirmationOpen = false
        Qt.callLater(function() {
            if (root.active)
                clearSearchButton.forceActiveFocus()
        })
    }

    function acknowledgeSearchHistoryCleared() {
        closeSearchClearConfirmation()
    }

    function openActivityClearConfirmation() {
        activityClearConfirmationOpen = true
        Qt.callLater(function() {
            if (root.active && root.activityClearConfirmationOpen)
                clearActivityCancel.forceActiveFocus()
        })
    }

    function closeActivityClearConfirmation() {
        activityClearConfirmationOpen = false
        Qt.callLater(function() {
            if (root.active)
                clearActivityButton.forceActiveFocus()
        })
    }

    function acknowledgeActivityHistoryCleared() {
        closeActivityClearConfirmation()
    }

    function openDeleteConfirmation() {
        deleteConfirmationOpen = true
        Qt.callLater(function() {
            if (root.active && root.deleteConfirmationOpen)
                deleteCancel.forceActiveFocus()
        })
    }

    function closeDeleteConfirmation() {
        deleteConfirmationOpen = false
        Qt.callLater(function() {
            if (root.active)
                deleteButton.forceActiveFocus()
        })
    }

    onActiveChanged: {
        if (!active)
            clearTransientState()
    }

    Component.onDestruction: clearTransientState()

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
            width: Math.min(980, scroller.width)
            height: implicitHeight
            spacing: 0

            Text {
                text: qsTr("Data & privacy")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 26
                font.weight: Font.DemiBold
                font.letterSpacing: -0.5
            }

            Item { width: 1; height: 34 }

            Text {
                width: parent.width
                visible: root.errorMessage.length > 0
                height: visible ? implicitHeight : 0
                text: root.errorMessage
                color: "#f0a3a3"
                font.family: theme.ui
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Accessible.name: qsTr("Data and privacy error: %1").arg(root.errorMessage)
            }

            Item {
                width: 1
                height: root.errorMessage.length > 0 ? 14 : 0
            }

            // HISTORY
            Column {
                id: historyGroup
                objectName: "privacyHistoryGroup"
                width: parent.width
                height: implicitHeight
                spacing: 0

                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme.edge
                }

                Item { width: 1; height: 23 }

                Text {
                    text: qsTr("History").toUpperCase()
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.letterSpacing: 1.0
                }

                Item { width: 1; height: 9 }

                // Search history
                Column {
                    width: parent.width
                    height: implicitHeight
                    spacing: 0

                    Item {
                        id: searchSettingRow
                        width: parent.width
                        height: root.compactRows
                            ? searchSettingMain.height + searchSettingActions.height + 14
                            : Math.max(searchSettingMain.height, searchSettingActions.height)

                        Row {
                            id: searchSettingMain
                            width: root.compactRows
                                ? parent.width
                                : Math.max(0, parent.width - searchSettingActions.width - 34)
                            height: Math.max(39, searchSettingCopy.height)
                            spacing: 15

                            Rectangle {
                                width: 39
                                height: 39
                                radius: 12
                                color: theme.glassTint
                                border.width: 1
                                border.color: theme.edge

                                AccountDataPrivacyIcon {
                                    anchors.centerIn: parent
                                    kind: "search"
                                    glyphSize: 18
                                    strokeColor: theme.inkDim
                                }
                            }

                            Column {
                                id: searchSettingCopy
                                width: Math.max(0, searchSettingMain.width - 54)
                                height: implicitHeight
                                spacing: 5

                                Text {
                                    width: parent.width
                                    text: qsTr("Search history")
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    width: Math.min(parent.width, 610)
                                    text: qsTr("Remember searches on this device. Search history never needs to leave this machine.")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    lineHeightMode: Text.ProportionalHeight
                                    lineHeight: 1.5
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Row {
                            id: searchSettingActions
                            spacing: 9
                            x: root.compactRows ? 54 : parent.width - width
                            y: root.compactRows
                                ? searchSettingMain.height + 14
                                : Math.max(0, (parent.height - height) / 2)

                            AccountButton {
                                id: clearSearchButton
                                objectName: "privacyClearSearchButton"
                                width: 100
                                height: 33
                                text: qsTr("Clear history")
                                enabled: !root.clearSearchHistoryBusy
                                Accessible.name: qsTr("Clear search history on this device")
                                onClicked: root.openSearchClearConfirmation()
                            }

                            AccountAuthoritativeSwitch {
                                objectName: "privacySearchHistorySwitch"
                                authoritativeChecked: root.rememberSearchHistory
                                busy: root.rememberSearchHistoryBusy
                                accessibleName: qsTr("Remember searches on this device")
                                onChangeRequested: function(enabled) {
                                    root.rememberSearchHistoryChangeRequested(enabled)
                                }
                            }
                        }
                    }

                    Column {
                        id: searchClearConfirm
                        objectName: "privacyClearSearchConfirm"
                        width: parent.width
                        height: visible ? implicitHeight : 0
                        visible: root.searchClearConfirmationOpen
                        spacing: 0

                        Item { width: 1; height: 16 }
                        Rectangle { width: parent.width; height: 1; color: root.edgeSoft }
                        Item { width: 1; height: 15 }

                        Item {
                            width: parent.width
                            height: root.compactRows
                                ? searchConfirmCopy.height + searchConfirmActions.height + 13
                                : Math.max(searchConfirmCopy.height, searchConfirmActions.height)

                            Text {
                                id: searchConfirmCopy
                                width: root.compactRows
                                    ? parent.width
                                    : Math.max(0, parent.width - searchConfirmActions.width - 24)
                                text: qsTr("Clear remembered searches from this device?")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                lineHeightMode: Text.ProportionalHeight
                                lineHeight: 1.5
                                wrapMode: Text.WordWrap
                            }

                            Row {
                                id: searchConfirmActions
                                spacing: 9
                                x: root.compactRows ? 0 : parent.width - width
                                y: root.compactRows ? searchConfirmCopy.height + 13 : 0

                                AccountButton {
                                    id: clearSearchCancel
                                    objectName: "privacyClearSearchCancel"
                                    width: 82
                                    height: 33
                                    text: qsTr("Cancel")
                                    enabled: !root.clearSearchHistoryBusy
                                    Accessible.name: qsTr("Cancel clearing search history")
                                    onClicked: root.closeSearchClearConfirmation()
                                }

                                AccountButton {
                                    objectName: "privacyClearSearchCommit"
                                    width: 142
                                    height: 33
                                    text: root.clearSearchHistoryBusy
                                        ? qsTr("Clearing…")
                                        : qsTr("Clear search history")
                                    variant: "primary"
                                    enabled: !root.clearSearchHistoryBusy
                                    Accessible.name: qsTr("Clear search history on this device")
                                    onClicked: root.clearSearchHistoryRequested()
                                }
                            }
                        }
                    }

                    Item { width: 1; height: 18 }
                    Rectangle { width: parent.width; height: 1; color: root.edgeSoft }
                }

                // Your Colosseum activity
                Column {
                    width: parent.width
                    height: implicitHeight
                    spacing: 0

                    Item { width: 1; height: 18 }

                    Item {
                        id: activitySettingRow
                        width: parent.width
                        height: root.compactRows
                            ? activitySettingMain.height + activitySettingActions.height + 14
                            : Math.max(activitySettingMain.height, activitySettingActions.height)

                        Row {
                            id: activitySettingMain
                            width: root.compactRows
                                ? parent.width
                                : Math.max(0, parent.width - activitySettingActions.width - 34)
                            height: Math.max(39, activitySettingCopy.height)
                            spacing: 15

                            Rectangle {
                                width: 39
                                height: 39
                                radius: 12
                                color: theme.glassTint
                                border.width: 1
                                border.color: theme.edge

                                AccountDataPrivacyIcon {
                                    anchors.centerIn: parent
                                    kind: "activity"
                                    glyphSize: 18
                                    strokeColor: theme.inkDim
                                }
                            }

                            Column {
                                id: activitySettingCopy
                                width: Math.max(0, activitySettingMain.width - 54)
                                height: implicitHeight
                                spacing: 5

                                Text {
                                    width: parent.width
                                    text: qsTr("Your Colosseum activity")
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    width: Math.min(parent.width, 610)
                                    text: qsTr("Keep watch and reading activity for Monthly Portrait, recent activity, completion history and personal media insights.")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    lineHeightMode: Text.ProportionalHeight
                                    lineHeight: 1.5
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Row {
                            id: activitySettingActions
                            spacing: 9
                            x: root.compactRows ? 54 : parent.width - width
                            y: root.compactRows
                                ? activitySettingMain.height + 14
                                : Math.max(0, (parent.height - height) / 2)

                            AccountButton {
                                id: clearActivityButton
                                objectName: "privacyClearActivityButton"
                                width: 104
                                height: 33
                                text: qsTr("Clear activity")
                                enabled: !root.clearActivityHistoryBusy
                                Accessible.name: qsTr("Clear Your Colosseum activity history")
                                onClicked: root.openActivityClearConfirmation()
                            }

                            AccountAuthoritativeSwitch {
                                objectName: "privacyActivityHistorySwitch"
                                authoritativeChecked: root.keepActivityHistory
                                busy: root.keepActivityHistoryBusy
                                accessibleName: qsTr("Keep watch and reading activity")
                                onChangeRequested: function(enabled) {
                                    root.keepActivityHistoryChangeRequested(enabled)
                                }
                            }
                        }
                    }

                    Column {
                        id: activityClearConfirm
                        objectName: "privacyClearActivityConfirm"
                        width: parent.width
                        height: visible ? implicitHeight : 0
                        visible: root.activityClearConfirmationOpen
                        spacing: 0

                        Item { width: 1; height: 16 }
                        Rectangle { width: parent.width; height: 1; color: root.edgeSoft }
                        Item { width: 1; height: 15 }

                        Item {
                            width: parent.width
                            height: root.compactRows
                                ? activityConfirmCopy.height + activityConfirmActions.height + 13
                                : Math.max(activityConfirmCopy.height, activityConfirmActions.height)

                            Text {
                                id: activityConfirmCopy
                                width: root.compactRows
                                    ? parent.width
                                    : Math.max(0, parent.width - activityConfirmActions.width - 24)
                                text: qsTr("Clear your stored watch and reading activity? Continue progress and Collection are not included.")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                lineHeightMode: Text.ProportionalHeight
                                lineHeight: 1.5
                                wrapMode: Text.WordWrap
                            }

                            Row {
                                id: activityConfirmActions
                                spacing: 9
                                x: root.compactRows ? 0 : parent.width - width
                                y: root.compactRows ? activityConfirmCopy.height + 13 : 0

                                AccountButton {
                                    id: clearActivityCancel
                                    objectName: "privacyClearActivityCancel"
                                    width: 82
                                    height: 33
                                    text: qsTr("Cancel")
                                    enabled: !root.clearActivityHistoryBusy
                                    Accessible.name: qsTr("Cancel clearing activity history")
                                    onClicked: root.closeActivityClearConfirmation()
                                }

                                AccountButton {
                                    objectName: "privacyClearActivityCommit"
                                    width: 140
                                    height: 33
                                    text: root.clearActivityHistoryBusy
                                        ? qsTr("Clearing…")
                                        : qsTr("Clear activity history")
                                    variant: "primary"
                                    enabled: !root.clearActivityHistoryBusy
                                    Accessible.name: qsTr("Clear watch and reading activity history")
                                    onClicked: root.clearActivityHistoryRequested()
                                }
                            }
                        }
                    }

                    Item { width: 1; height: 18 }
                    Rectangle { width: parent.width; height: 1; color: root.edgeSoft }
                }

                // Cross-device history
                Column {
                    width: parent.width
                    height: implicitHeight
                    spacing: 0

                    Item { width: 1; height: 18 }

                    Item {
                        id: syncSettingRow
                        width: parent.width
                        height: root.compactRows
                            ? syncSettingMain.height + syncHistorySwitch.height + 14
                            : Math.max(syncSettingMain.height, syncHistorySwitch.height)

                        Row {
                            id: syncSettingMain
                            width: root.compactRows
                                ? parent.width
                                : Math.max(0, parent.width - syncHistorySwitch.width - 34)
                            height: Math.max(39, syncSettingCopy.height)
                            spacing: 15

                            Rectangle {
                                width: 39
                                height: 39
                                radius: 12
                                color: theme.glassTint
                                border.width: 1
                                border.color: theme.edge

                                AccountDataPrivacyIcon {
                                    anchors.centerIn: parent
                                    kind: "sync"
                                    glyphSize: 18
                                    strokeColor: theme.inkDim
                                }
                            }

                            Column {
                                id: syncSettingCopy
                                width: Math.max(0, syncSettingMain.width - 54)
                                height: implicitHeight
                                spacing: 5

                                Text {
                                    width: parent.width
                                    text: qsTr("Cross-device history")
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    width: Math.min(parent.width, 610)
                                    text: qsTr("Sync watch and reading history between your signed-in devices. Turning this off can still leave activity available locally.")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    lineHeightMode: Text.ProportionalHeight
                                    lineHeight: 1.5
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        AccountAuthoritativeSwitch {
                            id: syncHistorySwitch
                            objectName: "privacySyncHistorySwitch"
                            authoritativeChecked: root.syncActivityHistory
                            busy: root.syncActivityHistoryBusy
                            accessibleName: qsTr("Sync watch and reading history")
                            x: root.compactRows ? 54 : parent.width - width
                            y: root.compactRows
                                ? syncSettingMain.height + 14
                                : Math.max(0, (parent.height - height) / 2)
                            onChangeRequested: function(enabled) {
                                root.syncActivityHistoryChangeRequested(enabled)
                            }
                        }
                    }

                    Item { width: 1; height: 18 }
                    Rectangle { width: parent.width; height: 1; color: theme.edge }
                }
            }

            Item { width: 1; height: 26 }

            // YOUR ACCOUNT DATA
            Column {
                id: accountDataGroup
                objectName: "privacyAccountDataGroup"
                width: parent.width
                height: implicitHeight
                spacing: 0

                Rectangle { width: parent.width; height: 1; color: theme.edge }
                Item { width: 1; height: 23 }

                Text {
                    text: qsTr("Your account data").toUpperCase()
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.letterSpacing: 1.0
                }

                Item { width: 1; height: 9 }

                Item {
                    id: exportSettingRow
                    width: parent.width
                    height: root.compactRows
                        ? exportSettingMain.height + exportButton.height + 14
                        : Math.max(exportSettingMain.height, exportButton.height)

                    Row {
                        id: exportSettingMain
                        width: root.compactRows
                            ? parent.width
                            : Math.max(0, parent.width - exportButton.width - 34)
                        height: Math.max(39, exportSettingCopy.height)
                        spacing: 15

                        Rectangle {
                            width: 39
                            height: 39
                            radius: 12
                            color: theme.glassTint
                            border.width: 1
                            border.color: theme.edge

                            AccountDataPrivacyIcon {
                                anchors.centerIn: parent
                                kind: "export"
                                glyphSize: 18
                                strokeColor: theme.inkDim
                            }
                        }

                        Column {
                            id: exportSettingCopy
                            width: Math.max(0, exportSettingMain.width - 54)
                            height: implicitHeight
                            spacing: 5

                            Text {
                                width: parent.width
                                text: qsTr("Export my data")
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: Math.min(parent.width, 610)
                                text: qsTr("Create a portable copy of the account data Colosseum holds for you.")
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                lineHeightMode: Text.ProportionalHeight
                                lineHeight: 1.5
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    AccountButton {
                        id: exportButton
                        objectName: "privacyExportButton"
                        width: 116
                        height: 33
                        x: root.compactRows ? 54 : parent.width - width
                        y: root.compactRows
                            ? exportSettingMain.height + 14
                            : Math.max(0, (parent.height - height) / 2)
                        text: root.dataExportBusy
                            ? qsTr("Requesting…")
                            : qsTr("Request export")
                        enabled: !root.dataExportBusy
                        Accessible.name: qsTr("Request export of my Colosseum account data")
                        onClicked: root.dataExportRequested()
                    }
                }

                Item { width: 1; height: 18 }
                Rectangle { width: parent.width; height: 1; color: theme.edge }
            }

            // Supporting privacy map
            Column {
                id: privacyMap
                objectName: "privacyMap"
                width: parent.width
                height: implicitHeight
                spacing: 0

                Item { width: 1; height: 35 }
                Rectangle { width: parent.width; height: 1; color: theme.edge }
                Item { width: 1; height: 27 }

                Text {
                    text: qsTr("What Colosseum keeps where")
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Item { width: 1; height: 4 }

                Text {
                    width: Math.min(parent.width, 720)
                    text: qsTr("These controls sit on top of a simple boundary: portable account state can follow you, while machine-specific and sensitive data stays outside ordinary sync.")
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    lineHeightMode: Text.ProportionalHeight
                    lineHeight: 1.55
                    wrapMode: Text.WordWrap
                }

                Item { width: 1; height: 18 }
                Rectangle { width: parent.width; height: 1; color: theme.edge }

                Grid {
                    id: privacyMapGrid
                    objectName: "privacyMapGrid"
                    width: parent.width
                    height: implicitHeight
                    columns: root.compactMap ? 1 : 2
                    rowSpacing: 0
                    columnSpacing: 0

                    Item {
                        id: portableLane
                        width: root.compactMap
                            ? privacyMapGrid.width
                            : privacyMapGrid.width / 2
                        height: portableLaneColumn.implicitHeight + 52

                        Column {
                            id: portableLaneColumn
                            x: 0
                            y: 25
                            width: root.compactMap
                                ? parent.width
                                : Math.max(0, parent.width - 30)
                            height: implicitHeight
                            spacing: 0

                            Row {
                                width: parent.width
                                height: Math.max(37, portableLaneHeadCopy.height)
                                spacing: 12

                                Rectangle {
                                    width: 37
                                    height: 37
                                    radius: 11
                                    color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.07)
                                    border.width: 1
                                    border.color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.36)

                                    AccountDataPrivacyIcon {
                                        anchors.centerIn: parent
                                        kind: "portable"
                                        glyphSize: 18
                                        strokeColor: theme.gold
                                    }
                                }

                                Column {
                                    id: portableLaneHeadCopy
                                    width: Math.max(0, portableLaneColumn.width - 49)
                                    height: implicitHeight
                                    spacing: 3

                                    Text {
                                        width: parent.width
                                        text: qsTr("Can follow your account")
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        text: qsTr("Portable state with a safe sync contract.")
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        lineHeightMode: Text.ProportionalHeight
                                        lineHeight: 1.45
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            Item { width: 1; height: 18 }

                            Repeater {
                                model: [
                                    { name: qsTr("Continue progress"), copy: qsTr("Portable resume points and progress.") },
                                    { name: qsTr("Collection"), copy: qsTr("Titles and shelves you save.") },
                                    { name: qsTr("History"), copy: qsTr("When cross-device history is enabled.") },
                                    { name: qsTr("Portable preferences"), copy: qsTr("Account-level choices with approved owners.") }
                                ]

                                Item {
                                    width: portableLaneColumn.width
                                    height: portableFactCopy.implicitHeight + (index === 0 ? 10 : 20)

                                    Rectangle {
                                        visible: index > 0
                                        width: parent.width
                                        height: 1
                                        color: root.edgeSoft
                                    }

                                    Rectangle {
                                        width: 5
                                        height: 5
                                        radius: 2.5
                                        x: 0
                                        y: (index === 0 ? 0 : 10) + 6
                                        color: theme.gold
                                    }

                                    Column {
                                        id: portableFactCopy
                                        x: 15
                                        y: index === 0 ? 0 : 10
                                        width: Math.max(0, parent.width - 15)
                                        height: implicitHeight
                                        spacing: 2

                                        Text {
                                            width: parent.width
                                            text: modelData.name
                                            color: theme.inkDim
                                            font.family: theme.ui
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            wrapMode: Text.WordWrap
                                        }

                                        Text {
                                            width: parent.width
                                            text: modelData.copy
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                            lineHeightMode: Text.ProportionalHeight
                                            lineHeight: 1.45
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        id: localLane
                        width: root.compactMap
                            ? privacyMapGrid.width
                            : privacyMapGrid.width / 2
                        height: localLaneColumn.implicitHeight + 52

                        Rectangle {
                            visible: !root.compactMap
                            width: 1
                            height: parent.height
                            color: theme.edge
                        }

                        Rectangle {
                            visible: root.compactMap
                            width: parent.width
                            height: 1
                            color: theme.edge
                        }

                        Column {
                            id: localLaneColumn
                            x: root.compactMap ? 0 : 30
                            y: 25
                            width: root.compactMap
                                ? parent.width
                                : Math.max(0, parent.width - 30)
                            height: implicitHeight
                            spacing: 0

                            Row {
                                width: parent.width
                                height: Math.max(37, localLaneHeadCopy.height)
                                spacing: 12

                                Rectangle {
                                    width: 37
                                    height: 37
                                    radius: 11
                                    color: theme.glassTint
                                    border.width: 1
                                    border.color: theme.edge

                                    AccountDataPrivacyIcon {
                                        anchors.centerIn: parent
                                        kind: "device"
                                        glyphSize: 18
                                        strokeColor: theme.inkDim
                                    }
                                }

                                Column {
                                    id: localLaneHeadCopy
                                    width: Math.max(0, localLaneColumn.width - 49)
                                    height: implicitHeight
                                    spacing: 3

                                    Text {
                                        width: parent.width
                                        text: qsTr("Stays on this device")
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        text: qsTr("Machine-owned data that ordinary sync does not carry.")
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        lineHeightMode: Text.ProportionalHeight
                                        lineHeight: 1.45
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            Item { width: 1; height: 18 }

                            Repeater {
                                model: [
                                    { name: qsTr("Search history"), copy: qsTr("Kept locally when search history is enabled.") },
                                    { name: qsTr("Window & session state"), copy: qsTr("Open sessions, geometry, PiP and machine state.") },
                                    { name: qsTr("Paths & media"), copy: qsTr("Local files and filesystem locations.") },
                                    { name: qsTr("Downloads & caches"), copy: qsTr("Machine-local acquisition and rebuildable data.") }
                                ]

                                Item {
                                    width: localLaneColumn.width
                                    height: localFactCopy.implicitHeight + (index === 0 ? 10 : 20)

                                    Rectangle {
                                        visible: index > 0
                                        width: parent.width
                                        height: 1
                                        color: root.edgeSoft
                                    }

                                    Rectangle {
                                        width: 5
                                        height: 5
                                        radius: 2.5
                                        x: 0
                                        y: (index === 0 ? 0 : 10) + 6
                                        color: theme.inkDimmer
                                    }

                                    Column {
                                        id: localFactCopy
                                        x: 15
                                        y: index === 0 ? 0 : 10
                                        width: Math.max(0, parent.width - 15)
                                        height: implicitHeight
                                        spacing: 2

                                        Text {
                                            width: parent.width
                                            text: modelData.name
                                            color: theme.inkDim
                                            font.family: theme.ui
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            wrapMode: Text.WordWrap
                                        }

                                        Text {
                                            width: parent.width
                                            text: modelData.copy
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                            lineHeightMode: Text.ProportionalHeight
                                            lineHeight: 1.45
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: theme.edge }

                Item {
                    width: parent.width
                    height: secretNoteCopy.implicitHeight + 46

                    Rectangle {
                        width: 35
                        height: 35
                        radius: 10
                        x: 0
                        y: 23
                        color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.06)
                        border.width: 1
                        border.color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.32)

                        AccountDataPrivacyIcon {
                            anchors.centerIn: parent
                            kind: "secret"
                            glyphSize: 17
                            strokeColor: theme.gold
                        }
                    }

                    Column {
                        id: secretNoteCopy
                        x: 48
                        y: 23
                        width: Math.max(0, parent.width - 48)
                        height: implicitHeight
                        spacing: 4

                        Text {
                            width: parent.width
                            text: qsTr("Secrets are a separate boundary.")
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: Math.min(parent.width, 760)
                            text: qsTr("Passwords, recovery keys, session tokens and API credentials do not enter ordinary account sync. Filesystem paths and secret-bearing fields are blocked from those payloads too.")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 10
                            lineHeightMode: Text.ProportionalHeight
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: theme.edge }
            }

            // ACCOUNT / destructive handoff
            Column {
                id: dangerZone
                objectName: "privacyDangerZone"
                width: parent.width
                height: implicitHeight
                spacing: 0

                Item { width: 1; height: 31 }

                Text {
                    text: qsTr("Account").toUpperCase()
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.letterSpacing: 1.0
                }

                Item { width: 1; height: 10 }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.28)
                }

                Item { width: 1; height: 19 }

                Item {
                    id: deleteSettingRow
                    width: parent.width
                    height: root.compactRows
                        ? deleteSettingCopy.height + deleteButton.height + 14
                        : Math.max(deleteSettingCopy.height, deleteButton.height)

                    Column {
                        id: deleteSettingCopy
                        width: root.compactRows
                            ? parent.width
                            : Math.max(0, parent.width - deleteButton.width - 30)
                        height: implicitHeight
                        spacing: 5

                        Text {
                            width: parent.width
                            text: qsTr("Delete Colosseum account")
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: Math.min(parent.width, 640)
                            text: qsTr("Permanently delete the account and its server-side account data. Local media files on your devices are not part of account deletion.")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            lineHeightMode: Text.ProportionalHeight
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                        }
                    }

                    Button {
                        id: deleteButton
                        objectName: "privacyDeleteButton"
                        width: 112
                        height: 33
                        padding: 0
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        x: root.compactRows ? 0 : parent.width - width
                        y: root.compactRows
                            ? deleteSettingCopy.height + 14
                            : Math.max(0, (parent.height - height) / 2)
                        enabled: !root.accountDeletionFlowBusy
                        Accessible.name: qsTr("Delete Colosseum account")
                        onClicked: root.openDeleteConfirmation()

                        contentItem: Text {
                            text: qsTr("Delete account")
                            color: deleteButton.enabled ? "#e0aaa7" : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 9
                            color: deleteButton.hovered
                                ? Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.09)
                                : "transparent"
                            border.width: 1
                            border.color: Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.45)
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            visible: deleteButton.activeFocus
                            radius: 12
                            color: "transparent"
                            border.width: 2
                            border.color: root.danger
                        }
                    }
                }

                Item { width: 1; height: 19 }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.28)
                }

                Column {
                    id: deleteConfirm
                    objectName: "privacyDeleteConfirm"
                    width: parent.width
                    height: visible ? implicitHeight : 0
                    visible: root.deleteConfirmationOpen
                    spacing: 0

                    Item { width: 1; height: 14 }

                    Rectangle {
                        width: parent.width
                        height: deleteConfirmColumn.implicitHeight + 32
                        radius: 12
                        color: Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.045)
                        border.width: 1
                        border.color: Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.30)

                        Column {
                            id: deleteConfirmColumn
                            x: 16
                            y: 16
                            width: parent.width - 32
                            height: implicitHeight
                            spacing: 0

                            Text {
                                width: parent.width
                                text: qsTr("This is permanent.")
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Item { width: 1; height: 5 }

                            Text {
                                width: parent.width
                                text: qsTr("Deleting the account removes its server-side identity and account data. A production implementation must require explicit re-authentication and final confirmation before deletion begins.")
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 10
                                lineHeightMode: Text.ProportionalHeight
                                lineHeight: 1.55
                                wrapMode: Text.WordWrap
                            }

                            Item { width: 1; height: 13 }

                            Row {
                                id: deleteConfirmActions
                                spacing: 9
                                x: root.compactRows ? 0 : parent.width - width

                                AccountButton {
                                    id: deleteCancel
                                    objectName: "privacyDeleteCancel"
                                    width: 82
                                    height: 33
                                    text: qsTr("Cancel")
                                    enabled: !root.accountDeletionFlowBusy
                                    Accessible.name: qsTr("Cancel account deletion")
                                    onClicked: root.closeDeleteConfirmation()
                                }

                                Button {
                                    objectName: "privacyDeleteContinue"
                                    width: 140
                                    height: 33
                                    padding: 0
                                    hoverEnabled: true
                                    focusPolicy: Qt.StrongFocus
                                    enabled: !root.accountDeletionFlowBusy
                                    Accessible.name: qsTr("Continue to Colosseum account deletion re-authentication")
                                    onClicked: root.accountDeletionFlowRequested()

                                    contentItem: Text {
                                        text: root.accountDeletionFlowBusy
                                            ? qsTr("Continuing…")
                                            : qsTr("Continue to delete")
                                        color: parent.enabled ? "#e0aaa7" : theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    background: Rectangle {
                                        radius: 9
                                        color: parent.hovered
                                            ? Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.09)
                                            : "transparent"
                                        border.width: 1
                                        border.color: Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.45)
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        visible: parent.activeFocus
                                        radius: 12
                                        color: "transparent"
                                        border.width: 2
                                        border.color: root.danger
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
