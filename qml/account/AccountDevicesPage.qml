// AccountDevicesPage.qml
// Native QML conversion of the locked Account Centre Devices mock.
// Device rows remain controller-authoritative; revoke never removes a row optimistically.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "accountDevicesPage"

    property var controller: null
    property bool active: false

    property var sortedDevices: []
    property bool refreshPending: false
    property bool refreshIssued: false
    property string revokeTargetId: ""
    property bool revokeRequestPending: false
    property string operationErrorMessage: ""

    readonly property bool signedIn: controller && controller.mode === "signedIn"
    readonly property bool compact: scroller.width < 820
    readonly property int visibleDeviceCount: sortedDevices ? sortedDevices.length : 0

    Theme { id: theme }
    readonly property color edgeSoft: Qt.rgba(1, 1, 1, 0.09)

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

    function textValue(item, key) {
        if (!item || item[key] === undefined || item[key] === null)
            return ""
        return String(item[key])
    }

    function currentDeviceId() {
        if (!controller || typeof controller.deviceId !== "function")
            return ""
        return String(controller.deviceId())
    }

    function deviceServerId(item) {
        return textValue(item, "id").trim()
    }

    function isCurrentDevice(item) {
        const id = deviceServerId(item)
        const current = currentDeviceId()
        return id.length > 0 && current.length > 0 && id === current
    }

    function timestampMs(item) {
        const raw = textValue(item, "last_seen_at").trim()
        if (raw.length === 0)
            return NaN
        const value = Date.parse(raw)
        return isFinite(value) ? value : NaN
    }

    function rebuildDevices() {
        const source = controller ? controller.devices : null
        const rows = []
        for (let i = 0; i < modelLength(source); ++i) {
            const entry = modelAt(source, i)
            if (entry)
                rows.push({ entry: entry, serviceIndex: i })
        }

        rows.sort(function(a, b) {
            const aCurrent = root.isCurrentDevice(a.entry)
            const bCurrent = root.isCurrentDevice(b.entry)
            if (aCurrent !== bCurrent)
                return aCurrent ? -1 : 1

            const aTime = root.timestampMs(a.entry)
            const bTime = root.timestampMs(b.entry)
            const aValid = isFinite(aTime)
            const bValid = isFinite(bTime)
            if (aValid !== bValid)
                return aValid ? -1 : 1
            if (aValid && bValid && aTime !== bTime)
                return bTime - aTime
            return a.serviceIndex - b.serviceIndex
        })

        const next = []
        for (let j = 0; j < rows.length; ++j)
            next.push(rows[j].entry)
        sortedDevices = next
        reconcileRevokeTarget()
    }

    function deviceAt(index) {
        if (!sortedDevices || index < 0 || index >= sortedDevices.length)
            return null
        return sortedDevices[index]
    }

    function findDevice(id) {
        for (let i = 0; i < visibleDeviceCount; ++i) {
            const item = deviceAt(i)
            if (deviceServerId(item) === id)
                return item
        }
        return null
    }

    function deviceLabel(item) {
        const label = textValue(item, "label").trim()
        return label.length > 0 ? label : qsTr("Unnamed device")
    }

    function relativeLastSeen(item) {
        const seen = timestampMs(item)
        if (!isFinite(seen))
            return ""

        const elapsed = Math.max(0, Date.now() - seen)
        const minute = 60 * 1000
        const hour = 60 * minute
        const day = 24 * hour

        if (elapsed < 2 * minute)
            return qsTr("last seen now")
        if (elapsed < hour) {
            const minutes = Math.max(2, Math.floor(elapsed / minute))
            return qsTr("last seen %1 minutes ago").arg(minutes)
        }
        if (elapsed < day) {
            const hours = Math.max(1, Math.floor(elapsed / hour))
            return hours === 1
                ? qsTr("last seen 1 hour ago")
                : qsTr("last seen %1 hours ago").arg(hours)
        }

        const days = Math.max(1, Math.floor(elapsed / day))
        return days === 1
            ? qsTr("last seen 1 day ago")
            : qsTr("last seen %1 days ago").arg(days)
    }

    function deviceMeta(item) {
        const platform = textValue(item, "platform").trim()
        const seen = relativeLastSeen(item)
        if (platform.length > 0 && seen.length > 0)
            return platform + " · " + seen
        if (platform.length > 0)
            return platform
        if (seen.length > 0)
            return seen
        return "—"
    }

    function countLabel() {
        return visibleDeviceCount === 1
            ? qsTr("trusted device")
            : qsTr("trusted devices")
    }

    function sectionCountLabel() {
        return visibleDeviceCount === 1
            ? qsTr("1 device")
            : qsTr("%1 devices").arg(visibleDeviceCount)
    }

    function requestRefresh() {
        if (!signedIn || !controller || refreshPending)
            return
        refreshPending = true
        operationErrorMessage = ""
        controller.refreshDevices()
    }

    function openRevoke(id) {
        const item = findDevice(id)
        if (!item || isCurrentDevice(item) || revokeRequestPending)
            return
        revokeTargetId = id
        operationErrorMessage = ""
    }

    function cancelRevoke() {
        if (revokeRequestPending)
            return
        revokeTargetId = ""
        operationErrorMessage = ""
    }

    function confirmRevoke(id) {
        if (!signedIn
            || !controller
            || revokeRequestPending
            || revokeTargetId !== id) {
            return
        }

        const item = findDevice(id)
        if (!item || isCurrentDevice(item)) {
            reconcileRevokeTarget()
            return
        }

        revokeRequestPending = true
        operationErrorMessage = ""
        controller.revokeDevice(id)
    }

    function reconcileRevokeTarget() {
        if (revokeTargetId.length === 0)
            return
        if (!findDevice(revokeTargetId)) {
            revokeTargetId = ""
            revokeRequestPending = false
            operationErrorMessage = ""
        }
    }

    function clearEphemeralState() {
        refreshPending = false
        refreshIssued = false
        revokeTargetId = ""
        revokeRequestPending = false
        operationErrorMessage = ""
    }

    onControllerChanged: {
        refreshIssued = false
        rebuildDevices()
        if (active) {
            refreshIssued = true
            requestRefresh()
        }
    }

    onActiveChanged: {
        if (!active) {
            clearEphemeralState()
            return
        }

        rebuildDevices()
        if (!refreshIssued) {
            refreshIssued = true
            requestRefresh()
        }
    }

    Component.onCompleted: {
        rebuildDevices()
        if (active && !refreshIssued) {
            refreshIssued = true
            requestRefresh()
        }
    }

    Connections {
        target: root.controller
        enabled: root.controller !== null
        ignoreUnknownSignals: true

        function onDevicesChanged() {
            root.rebuildDevices()
        }

        function onDeviceListRefreshSucceeded() {
            root.refreshPending = false
            root.rebuildDevices()
        }

        function onDeviceListRefreshFailed(message) {
            root.refreshPending = false
            root.revokeRequestPending = false
            root.operationErrorMessage = message
                || qsTr("Could not refresh trusted devices.")
        }

        function onDeviceRevokeSucceeded(deviceId) {
            const reconciledId = String(deviceId)
            if (reconciledId !== root.revokeTargetId)
                return

            // For remote devices the controller emits this only after its
            // authoritative list refresh has completed. Never launch a second
            // QML refresh phase and never remove the row locally.
            root.refreshPending = false
            root.rebuildDevices()
            root.revokeRequestPending = false

            if (!root.findDevice(reconciledId))
                root.reconcileRevokeTarget()
        }

        function onDeviceRevokeFailed(deviceId, message) {
            if (String(deviceId) !== root.revokeTargetId)
                return
            root.revokeRequestPending = false
            root.operationErrorMessage = message
                || qsTr("Could not revoke this device.")
        }

        function onSignedOut() {
            root.clearEphemeralState()
        }

        function onCurrentDeviceLocked() {
            root.clearEphemeralState()
        }
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
            width: Math.min(980, scroller.width)
            spacing: 0

            Item {
                width: parent.width
                height: Math.max(pageTitle.implicitHeight, refreshButton.height)

                Text {
                    id: pageTitle
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Devices")
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.5
                }

                AccountButton {
                    id: refreshButton
                    objectName: "devicesRefreshButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 82
                    height: 32
                    text: root.refreshPending ? qsTr("Refreshing…") : qsTr("Refresh")
                    enabled: root.signedIn && !root.refreshPending
                    Accessible.name: qsTr("Refresh trusted devices")
                    onClicked: root.requestRefresh()
                }
            }

            Item { width: 1; height: 30 }

            Column {
                id: summary
                objectName: "devicesSummary"
                width: parent.width
                spacing: 0

                Rectangle { width: parent.width; height: 1; color: theme.edge }
                Item { width: 1; height: 24 }

                Item {
                    width: parent.width
                    height: root.compact
                        ? summaryCountColumn.height + summaryNote.height + 22
                        : Math.max(summaryCountColumn.height, summaryNote.height)

                    Column {
                        id: summaryCountColumn
                        width: root.compact ? parent.width : 260
                        spacing: 8

                        Text {
                            objectName: "devicesSummaryCount"
                            text: root.visibleDeviceCount
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 42
                            font.weight: Font.DemiBold
                            font.letterSpacing: -1.8
                        }

                        Text {
                            text: root.countLabel()
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                        }
                    }

                    Text {
                        id: summaryNote
                        x: root.compact ? 0 : parent.width - width
                        y: root.compact ? summaryCountColumn.height + 22 : 0
                        width: root.compact ? parent.width : Math.min(440, parent.width - 300)
                        text: qsTr("Revoking a device ends its account access. It can sign in again later if you approve it.")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        lineHeightMode: Text.ProportionalHeight
                        lineHeight: 1.55
                        wrapMode: Text.WordWrap
                        horizontalAlignment: root.compact ? Text.AlignLeft : Text.AlignRight
                    }
                }

                Item { width: 1; height: 24 }
                Rectangle { width: parent.width; height: 1; color: theme.edge }
            }

            Text {
                width: parent.width
                visible: root.operationErrorMessage.length > 0
                height: visible ? implicitHeight + 18 : 0
                topPadding: 18
                text: root.operationErrorMessage
                color: "#f0a3a3"
                font.family: theme.ui
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Accessible.name: qsTr("Devices error: %1").arg(root.operationErrorMessage)
            }

            Item { width: 1; height: 30 }

            Item {
                width: parent.width
                height: 16

                Text {
                    anchors.left: parent.left
                    text: qsTr("Trusted devices")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    text: root.sectionCountLabel().toUpperCase()
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.letterSpacing: 0.8
                }
            }

            Item { width: 1; height: 13 }
            Rectangle { width: parent.width; height: 1; color: theme.edge }

            Text {
                objectName: "devicesEmptyState"
                width: parent.width
                visible: root.visibleDeviceCount === 0
                height: visible ? implicitHeight + 60 : 0
                topPadding: 30
                bottomPadding: 30
                text: qsTr("No trusted devices were returned by the account service.")
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: root.visibleDeviceCount

                Column {
                    id: deviceDelegate
                    width: pageColumn.width
                    spacing: 0

                    property var entry: root.deviceAt(index)
                    property string serverId: root.deviceServerId(entry)
                    property bool currentDevice: root.isCurrentDevice(entry)
                    property bool confirmationOpen:
                        !currentDevice
                        && serverId.length > 0
                        && root.revokeTargetId === serverId

                    Item {
                        objectName: "deviceRow_" + deviceDelegate.serverId
                        width: parent.width
                        height: Math.max(78, deviceRowMain.height + 30)

                        Row {
                            id: deviceRowMain
                            x: 0
                            y: 15
                            width: root.compact
                                ? Math.max(0, parent.width - 108)
                                : Math.max(0, parent.width - (deviceDelegate.currentDevice ? 0 : 96))
                            spacing: 15

                            Rectangle {
                                width: 42
                                height: 42
                                radius: 12
                                color: deviceDelegate.currentDevice
                                    ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.08)
                                    : theme.glassTint
                                border.width: 1
                                border.color: deviceDelegate.currentDevice
                                    ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.45)
                                    : theme.edge

                                AccountDeviceIcon {
                                    anchors.centerIn: parent
                                    glyphSize: 20
                                    strokeColor: deviceDelegate.currentDevice
                                        ? theme.gold
                                        : theme.inkDim
                                }
                            }

                            Column {
                                width: Math.max(0, deviceRowMain.width - 57)
                                spacing: 5

                                Row {
                                    width: parent.width
                                    spacing: 9

                                    Text {
                                        width: Math.max(
                                            0,
                                            parent.width
                                                - (deviceDelegate.currentDevice
                                                    ? currentMark.implicitWidth + 9
                                                    : 0))
                                        text: root.deviceLabel(deviceDelegate.entry)
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        id: currentMark
                                        objectName: "deviceCurrentMark_" + deviceDelegate.serverId
                                        visible: deviceDelegate.currentDevice
                                        text: qsTr("This device").toUpperCase()
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 9
                                        font.letterSpacing: 0.9
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: root.deviceMeta(deviceDelegate.entry)
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        AccountButton {
                            id: revokeButton
                            objectName: "deviceRevoke_" + deviceDelegate.serverId
                            visible: !deviceDelegate.currentDevice
                                && deviceDelegate.serverId.length > 0
                            width: visible ? 82 : 0
                            height: 32
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Revoke")
                            enabled: root.signedIn
                                && !root.revokeRequestPending
                            Accessible.name: qsTr("Revoke %1").arg(root.deviceLabel(deviceDelegate.entry))
                            onClicked: {
                                root.openRevoke(deviceDelegate.serverId)
                                Qt.callLater(function() {
                                    if (deviceDelegate.confirmationOpen)
                                        cancelButton.forceActiveFocus()
                                })
                            }
                        }
                    }

                    Column {
                        id: confirmArea
                        objectName: "deviceConfirm_" + deviceDelegate.serverId
                        width: parent.width
                        visible: deviceDelegate.confirmationOpen
                        height: visible ? implicitHeight : 0
                        spacing: 0

                        Rectangle { width: parent.width; height: 1; color: root.edgeSoft }
                        Item { width: 1; height: 14 }

                        Item {
                            width: parent.width
                            height: root.compact
                                ? confirmCopy.implicitHeight + confirmButtons.height + 13
                                : Math.max(confirmCopy.implicitHeight, confirmButtons.height)

                            Text {
                                id: confirmCopy
                                width: root.compact
                                    ? parent.width
                                    : Math.max(0, parent.width - confirmButtons.width - 24)
                                text: qsTr("Revoke %1? It will lose account access and need to sign in again.")
                                    .arg(root.deviceLabel(deviceDelegate.entry))
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                lineHeightMode: Text.ProportionalHeight
                                lineHeight: 1.5
                                wrapMode: Text.WordWrap
                            }

                            Row {
                                id: confirmButtons
                                x: root.compact ? 0 : parent.width - width
                                y: root.compact ? confirmCopy.implicitHeight + 13 : 0
                                spacing: 9

                                AccountButton {
                                    id: cancelButton
                                    objectName: "deviceRevokeCancel_" + deviceDelegate.serverId
                                    width: 82
                                    height: 32
                                    text: qsTr("Cancel")
                                    enabled: !root.revokeRequestPending
                                    Accessible.name: qsTr("Cancel revoking %1").arg(root.deviceLabel(deviceDelegate.entry))
                                    onClicked: root.cancelRevoke()
                                }

                                AccountButton {
                                    objectName: "deviceRevokeConfirm_" + deviceDelegate.serverId
                                    width: 124
                                    height: 32
                                    text: root.revokeRequestPending
                                        ? qsTr("Revoking…")
                                        : qsTr("Confirm revoke")
                                    variant: "primary"
                                    enabled: !root.revokeRequestPending
                                    Accessible.name: qsTr("Confirm revoke %1").arg(root.deviceLabel(deviceDelegate.entry))
                                    onClicked: root.confirmRevoke(deviceDelegate.serverId)
                                }
                            }
                        }

                        Item { width: 1; height: 16 }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: theme.edge
                    }
                }
            }
        }
    }
}
