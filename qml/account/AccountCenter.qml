// AccountCenter.qml
// Composed Account Centre candidate against the current live Colosseum shell.
// All six locked page packages coexist here so transplantation does not depend
// on stacking mutually-overlapping AccountCenter patches.

import QtQuick
import QtQuick.Controls
import ".."
import "../SystemFocusContainment.js" as SystemFocusContainment

SettingsScaffold {
    id: root
    objectName: "accountCenter"

    property var controller: null
    property var feriaController: null
    property bool feriaOnly: false
    property var ownedFeriaView: null
    signal feriaActivityRequested(string section)
    function configureFeriaContent() {
        if (!feriaController || !feriaContent.item) return
        ownedFeriaView = feriaContent.item
        feriaController.sharedAccountView = ownedFeriaView
        feriaController.sharedAccountActive = true
        feriaController.accountTab = "apps"
    }
    function releaseFeriaContent() {
        if (feriaController && ownedFeriaView && feriaController.sharedAccountView === ownedFeriaView) {
            feriaController.sharedAccountActive = false
            feriaController.sharedAccountView = null
        }
        ownedFeriaView = null
    }
    onFeriaControllerChanged: configureFeriaContent()
    Connections {
        target: root.feriaController
        function onAccountTabChanged() {
            if (root.visible && root.ownedFeriaView && root.feriaController.accountTab !== "apps")
                root.feriaActivityRequested(root.feriaController.accountTab)
        }
    }
    Connections {
        target: typeof FeriaAccount !== "undefined" ? FeriaAccount : null
        function onProfileChanged() { root.close() }
    }
    property var recoveryPresenter: null
    readonly property bool accountPresent: controller
        && (controller.mode === "signedIn" || controller.mode === "offline")
    readonly property bool onlineAccount: controller
        && controller.mode === "signedIn"
    property string initial: "?"
    property string activeSection: "profile"
    property Item focusReturnItem: null
    // No Feria section on Android: qml/feria/ is not shipped there, so neither the rail
    // entry nor the feriaContent Loader may reference it (feriaOnly itself is unreachable).
    property bool feriaEnabled: false
    readonly property bool feriaSectionAvailable: Qt.platform.os !== "android" && feriaEnabled
    readonly property var railSections: feriaOnly && feriaSectionAvailable ? [{ id: "feria", label: qsTr("Feria"), glyph: "▣" }] : feriaSectionAvailable ? [
        { id: "profile", label: qsTr("Profile"), glyph: "◌" },
        { id: "feria", label: qsTr("Feria"), glyph: "▣" },
        { id: "security", label: qsTr("Security"), glyph: "◇" },
        { id: "devices", label: qsTr("Devices"), glyph: "▣" },
        { id: "recovery", label: qsTr("Recovery"), glyph: "↶" },
        { id: "privacy", label: qsTr("Data & privacy"), glyph: "◫" }
    ] : [
        { id: "profile", label: qsTr("Profile"), glyph: "◌" },
        { id: "security", label: qsTr("Security"), glyph: "◇" },
        { id: "devices", label: qsTr("Devices"), glyph: "▣" },
        { id: "recovery", label: qsTr("Recovery"), glyph: "↶" },
        { id: "privacy", label: qsTr("Data & privacy"), glyph: "◫" }
    ]
    readonly property int activeRailIndex: {
        for (let i = 0; i < railSections.length; ++i) {
            if (railSections[i].id === activeSection)
                return i
        }
        return 0
    }
    readonly property bool compactPhoneLayout:layout.touch && !layout.canFitRail(232)

    property var preferencesStore:
        typeof ProfilePreferences !== "undefined" ? ProfilePreferences : null
    property var historyCoordinator:
        typeof ProfileConsumptionHistory !== "undefined" ? ProfileConsumptionHistory : null
    // F0010: the Remember-searches switch reads the search-history store's own durable
    // rememberEnabled policy (the enforced authority); the preferences lane feeds the
    // runtime retention projection and owns the activity switches.
    readonly property bool privacyRememberSearchHistory:
        searchHistoryStore ? searchHistoryStore.rememberEnabled : true
    readonly property bool privacyKeepActivityHistory:
        preferencesStore ? preferencesStore.keepActivityHistory : true
    readonly property bool privacySyncActivityHistory:
        preferencesStore ? preferencesStore.syncActivityHistory : true
    property bool privacyRememberSearchHistoryBusy: false
    property bool privacyKeepActivityHistoryBusy: false
    property bool privacySyncActivityHistoryBusy: false
    property bool privacyClearSearchHistoryBusy: false
    property bool privacyClearActivityHistoryBusy: false
    property bool privacyDataExportBusy: false
    property bool privacyAccountDeletionFlowBusy: false
    property string privacyErrorMessage: ""
    property string privacyStatusMessage: ""

    signal privacyRememberSearchHistoryChangeRequested(bool enabled)
    signal privacyKeepActivityHistoryChangeRequested(bool enabled)
    signal privacySyncActivityHistoryChangeRequested(bool enabled)
    signal privacyClearSearchHistoryRequested()
    signal privacyClearActivityHistoryRequested()
    signal privacyDataExportRequested()
    signal privacyDataExportCancelRequested()
    signal privacyAccountDeletionFlowRequested(string currentPassword)

    onPrivacyRememberSearchHistoryChangeRequested: function(enabled) {
        if (searchHistoryStore) searchHistoryStore.rememberEnabled = enabled
        if (preferencesStore) preferencesStore.setRememberSearchHistory(enabled)
    }
    onPrivacyKeepActivityHistoryChangeRequested: function(enabled) {
        if (preferencesStore) preferencesStore.setKeepActivityHistory(enabled)
    }
    onPrivacySyncActivityHistoryChangeRequested: function(enabled) {
        if (preferencesStore) preferencesStore.setSyncActivityHistory(enabled)
    }

    // E2/E3 backend wiring (roadmap §9, CPP-PORT-CONTRACT.md §16 "Deletion and user-control
    // rules"): the two clears that have real existing local owners. Exposed as PROPERTIES
    // (not a bare-global reference inside the handler) so a host/test can inject a fake —
    // the same injection shape `controller`/`recoveryPresenter` already use above — while
    // the real app picks up the native SearchHistoryStore/ActivityStore context properties
    // automatically via the typeof-guarded default, same pattern as colosseumEarliestMonthKey.
    property var searchHistoryStore: typeof SearchHistory !== "undefined" ? SearchHistory : null

    // The three real remembered search scopes, verified 2026-08-19 by grepping every
    // SearchHistoryStore record()/list() call site in qml/ rather than trusting a guess:
    // BiblioSearch.qml hardcodes "biblio"; SearchSurface.qml (shared by the Tankoban and
    // Theatre worlds) derives its scope from searchMode.toLowerCase(), and Main.qml only
    // ever sets searchMode to "Tankoban" or "Theatre". No "all"/"home"/"world" scope exists
    // in production use — E2 clears exactly these three, nothing else.
    readonly property var privacySearchHistoryScopes: ["biblio", "tankoban", "theatre", "vinyl"]

    // E2: aggregate local search-history clear via the real SearchHistoryStore owner.
    onPrivacyClearSearchHistoryRequested: {
        if (searchHistoryStore)
            searchHistoryStore.clearAllScopes(privacySearchHistoryScopes)
    }

    // E3: activity-history clear via the real ActivityStore owner (QML name
    // ProfileActivity). Continue (ProgressStore) and Collection (CollectionStore) are
    // separate stores this handler never touches — CPP-PORT-CONTRACT.md §16 forbids
    // ProgressStore::forget/HistoryStore::remove from secretly deleting activity, and the
    // inverse holds here: clearing activity never reaches into Progress or Collection.
    onPrivacyClearActivityHistoryRequested: {
        privacyErrorMessage = ""
        if (!historyCoordinator || !historyCoordinator.clearAll()) {
            privacyErrorMessage = qsTr("Could not clear all stored watch and reading activity.")
            return
        }
        privacyPage.acknowledgeActivityHistoryCleared()
    }

    visible: false
    anchors.fill: parent
    // Same z as the Taskbar (900) and the AccountOnboardingHost (900): ties resolve by later
    // sibling/document order in Main.qml, and AccountCenter is instantiated after the Taskbar
    // (so it draws above the Taskbar dock and no longer loses clicks to it) and before the
    // onboarding host (so onboarding still draws on top of the Centre).
    z: 900
    color: "#0d0c09"
    opacity: 0

    Behavior on opacity { NumberAnimation { duration: 160 } }
    onVisibleChanged: {
        opacity = visible ? 1 : 0
        if (!visible) releaseFeriaContent()
    }

    function rememberInvoker(invoker) {
        if (invoker) {
            focusReturnItem = invoker
            return
        }
        const active = root.Window.window ? root.Window.window.activeFocusItem : null
        if (active && !SystemFocusContainment.isWithin(active, root))
            focusReturnItem = active
    }

    function focusRail() {
        Qt.callLater(function() {
            if (!root.visible)
                return
            if (root.compactPhoneLayout) {
                compactSections.forceActiveFocus()
                compactSections.positionViewAtIndex(root.activeRailIndex, ListView.Contain)
            } else {
                railColumn.forceActiveFocus()
            }
        })
    }

    function restoreInvoker() {
        const target = focusReturnItem
        focusReturnItem = null
        Qt.callLater(function() {
            if (target && target.visible && target.enabled)
                target.forceActiveFocus()
        })
    }

    function runSessionAction() {
        if (root.controller) {
            if (root.accountPresent)
                root.controller.logoutCurrent()
            else
                root.controller.returnToSignIn()
        }
        root.close()
    }

    function open(section, invoker, context) {
        if (!root.visible) {
            rememberInvoker(invoker)
            feriaOnly = context === "feria" && feriaSectionAvailable
        }
        activeSection = feriaOnly ? "feria"
            : railSections.some(function(row) { return row.id === section }) ? section : "profile"
        root.visible = true
        focusRail()
    }

    function close() {
        if (!root.visible)
            return
        root.visible = false
        restoreInvoker()
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.close()
            event.accepted = true
        } else if (event.key === Qt.Key_Tab) {
            const forward = !(event.modifiers & Qt.ShiftModifier)
            if (SystemFocusContainment.move(root.Window.window, root, forward))
                event.accepted = true
        }
    }

    Shortcut {
        sequence: "Tab"
        enabled: root.visible
        onActivated: SystemFocusContainment.move(root.Window.window, root, true)
    }
    Shortcut {
        sequence: "Shift+Tab"
        enabled: root.visible
        onActivated: SystemFocusContainment.move(root.Window.window, root, false)
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.45
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: !root.compactPhoneLayout
            width: root.compactPhoneLayout ? 0 : 232
            height: parent.height
            color: "#121009"

            Column {
                id: railColumn
                objectName: "accountCenterRailRegion"
                x: 20
                y: 74
                width: parent.width - 40
                spacing: 4
                visible: !root.compactPhoneLayout
                activeFocusOnTab: root.visible && visible
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Account Centre sections")

                Keys.onPressed: function(event) {
                    let next = root.activeRailIndex
                    if (event.key === Qt.Key_Up)
                        next -= 1
                    else if (event.key === Qt.Key_Down)
                        next += 1
                    else if (event.key === Qt.Key_Home)
                        next = 0
                    else if (event.key === Qt.Key_End)
                        next = root.railSections.length - 1
                    else if (event.key === Qt.Key_Return
                             || event.key === Qt.Key_Enter
                             || event.key === Qt.Key_Space) {
                        event.accepted = true
                        return
                    } else {
                        return
                    }

                    if (next >= 0 && next < root.railSections.length) {
                        root.activeSection = root.railSections[next].id
                        event.accepted = true
                    } else {
                        event.accepted = false
                    }
                }

                Row {
                    spacing: 12
                    bottomPadding: 18

                    Rectangle {
                        width: 38
                        height:38
                        radius: 19
                        color: Qt.rgba(0.94, 0.77, 0.29, 0.14)
                        border.width: 1.5
                        border.color: Qt.rgba(0.94, 0.77, 0.29, 0.75)

                        Text {
                            anchors.centerIn: parent
                            text: root.feriaOnly ? "F" : root.accountPresent ? root.initial : "?"
                            color: "#f0df9a"
                            font.family: "Inter"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                    }

                    Column {
                        spacing: 1
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            objectName: "accountCenterUsername"
                            text: root.feriaOnly ? qsTr("Feria settings") : root.accountPresent && root.controller
                                ? root.controller.username
                                : qsTr("Not signed in")
                            color: "#f2f2ef"
                            font.family: "Inter"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: root.feriaOnly ? qsTr("Apps and services") : qsTr("Colosseum account")
                            color: "#7d7a6f"
                            font.family: "Inter"
                            font.pixelSize: 10
                        }
                    }
                }

                Repeater {
                    model: root.railSections

                    Rectangle {
                        objectName: "accountCenterRail_" + modelData.id
                        width: parent ? parent.width : 0
                        height:root.layout.touch ? root.layout.touchTargetSize : 38
                        radius: 9
                        color: root.activeSection === modelData.id
                            ? Qt.rgba(0.94, 0.77, 0.29, 0.12)
                            : (railMa.containsMouse
                                ? Qt.rgba(1, 1, 1, 0.05)
                                : "transparent")
                        border.width: railColumn.activeFocus
                            && root.activeSection === modelData.id ? 2 : 0
                        border.color: "#f0df9a"

                        Row {
                            x: 12
                            spacing: 10
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                text: modelData.glyph
                                color: root.activeSection === modelData.id
                                    ? "#f0df9a"
                                    : "#8f8b80"
                                font.pixelSize: 13
                            }

                            Text {
                                text: modelData.label
                                color: root.activeSection === modelData.id
                                    ? "#f2f2ef"
                                    : "#b7b3a6"
                                font.family: "Inter"
                                font.pixelSize: 13
                                font.weight: root.activeSection === modelData.id
                                    ? Font.DemiBold
                                    : Font.Normal
                            }
                        }

                        MouseArea {
                            id: railMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.activeSection = modelData.id
                        }
                    }
                }

                Item { width: 1; height: 14 }

                Text {
                    id: railSessionAction
                    visible: !root.feriaOnly
                    text: root.accountPresent ? qsTr("Sign out of Colosseum") : qsTr("Sign in to Colosseum")
                    color: railSessionAction.activeFocus || railOutMa.containsMouse
                        ? "#d8d4c8" : "#8f8b80"
                    font.family: "Inter"
                    font.pixelSize: 12
                    activeFocusOnTab: root.visible && visible
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Keys.onReturnPressed: root.runSessionAction()
                    Keys.onEnterPressed: root.runSessionAction()
                    Keys.onSpacePressed: root.runSessionAction()

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: -5
                        height: 1
                        visible: railSessionAction.activeFocus
                        color: "#f0df9a"
                    }

                    MouseArea {
                        id: railOutMa
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.runSessionAction()
                    }
                }
            }

            Rectangle {
                width: 1
                height: parent.height
                anchors.right: parent.right
                color: "#221f18"
            }
        }

        Item {
            id: accountContent
            width: parent.width - (root.compactPhoneLayout ? 0 : 232)
            height: parent.height

            readonly property int pageHorizontalInset: root.compactPhoneLayout ? 16 : 34
            readonly property int pageTopInset: root.compactPhoneLayout ? 112 : 64
            readonly property int pageBottomInset: root.compactPhoneLayout ? 16 : 32

            ListView {
                id: compactSections
                objectName: "accountCenterCompactSections"
                visible: root.compactPhoneLayout
                x: 16
                y: 12
                width: parent.width - 32
                height: 48
                orientation: ListView.Horizontal
                spacing: 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.railSections
                currentIndex: root.activeRailIndex
                activeFocusOnTab: root.visible && visible
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Account Centre sections")

                onCurrentIndexChanged: {
                    if (currentIndex >= 0)
                        positionViewAtIndex(currentIndex, ListView.Contain)
                }

                Keys.onPressed: function(event) {
                    let next = root.activeRailIndex
                    if (event.key === Qt.Key_Left)
                        next -= 1
                    else if (event.key === Qt.Key_Right)
                        next += 1
                    else if (event.key === Qt.Key_Home)
                        next = 0
                    else if (event.key === Qt.Key_End)
                        next = root.railSections.length - 1
                    else if (event.key === Qt.Key_Return
                             || event.key === Qt.Key_Enter
                             || event.key === Qt.Key_Space) {
                        event.accepted = true
                        return
                    } else {
                        return
                    }

                    if (next >= 0 && next < root.railSections.length) {
                        root.activeSection = root.railSections[next].id
                        event.accepted = true
                    } else {
                        event.accepted = false
                    }
                }

                delegate: Rectangle {
                    required property var modelData
                    width: compactLabel.implicitWidth + 40
                    height: compactSections.height
                    radius: 9
                    color: root.activeSection === modelData.id
                        ? Qt.rgba(0.94, 0.77, 0.29, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: compactSections.activeFocus
                        && root.activeSection === modelData.id ? 2 : 0
                    border.color: "#f0df9a"
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label

                    Row {
                        anchors.centerIn: parent
                        spacing: 7

                        Text {
                            text: modelData.glyph
                            color: root.activeSection === modelData.id ? "#f0df9a" : "#8f8b80"
                            font.pixelSize: 13
                        }
                        Text {
                            id: compactLabel
                            text: modelData.label
                            color: root.activeSection === modelData.id ? "#f2f2ef" : "#b7b3a6"
                            font.family: "Inter"
                            font.pixelSize: 12
                            font.weight: root.activeSection === modelData.id
                                ? Font.DemiBold : Font.Normal
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeSection = modelData.id
                    }
                }
            }

            Text {
                id: backAction
                x: root.compactPhoneLayout ? 16 : 34
                y: root.compactPhoneLayout ? 60 : 30
                width: root.compactPhoneLayout ? Math.max(48, implicitWidth + 16) : implicitWidth
                height: root.compactPhoneLayout ? 48 : implicitHeight
                text: qsTr("‹ Back")
                color: backAction.activeFocus || backMa.containsMouse
                    ? "#b7b3a6" : "#8f8b80"
                font.family: "Inter"
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                activeFocusOnTab: root.visible
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Back from Account Centre")
                Keys.onReturnPressed: root.close()
                Keys.onEnterPressed: root.close()
                Keys.onSpacePressed: root.close()

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -5
                    height: 1
                    visible: backAction.activeFocus
                    color: "#f0df9a"
                }

                MouseArea {
                    id: backMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }

            Text {
                id: compactSessionAction
                visible: root.compactPhoneLayout && !root.feriaOnly
                anchors.right: parent.right
                anchors.rightMargin: 16
                y: 60
                width: Math.max(48, implicitWidth + 16)
                height: 48
                text: root.accountPresent ? qsTr("Sign out of Colosseum") : qsTr("Sign in to Colosseum")
                color: activeFocus || compactSessionMa.containsMouse ? "#d8d4c8" : "#8f8b80"
                font.family: "Inter"
                font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                activeFocusOnTab: root.visible && visible
                Accessible.role: Accessible.Button
                Accessible.name: text
                Keys.onReturnPressed: root.runSessionAction()
                Keys.onEnterPressed: root.runSessionAction()
                Keys.onSpacePressed: root.runSessionAction()

                MouseArea {
                    id: compactSessionMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.runSessionAction()
                }
            }

            AccountProfilePage {
                x: accountContent.pageHorizontalInset
                y: accountContent.pageTopInset
                width: accountContent.width - accountContent.pageHorizontalInset * 2
                height: accountContent.height - accountContent.pageTopInset - accountContent.pageBottomInset
                active: root.visible && !root.feriaOnly && root.activeSection === "profile"
                visible: active
                controller: root.controller
            }

            Loader {
                id: feriaContent
                objectName: "accountFeriaContent"
                x: 20; y: 64
                width: parent.width - 40; height: parent.height - y - 24
                active: root.visible && root.feriaController !== null
                    && root.activeSection === "feria"
                visible: active
                // String source (desktop-only path) so the Android bundle, which ships no
                // qml/feria/, never parses PorticoCombinedParityAccountView.qml.
                source: "../feria/PorticoCombinedParityAccountView.qml"
                onLoaded: {
                    item.embedded = true
                    item.servicesOnly = true
                    root.configureFeriaContent()
                }
                onActiveChanged: if (!active) root.releaseFeriaContent()
                Binding {
                    target: feriaContent.item
                    property: "controller"
                    value: root.feriaController
                }
            }

            AccountSecurityPage {
                x: accountContent.pageHorizontalInset
                y: accountContent.pageTopInset
                width: accountContent.width - accountContent.pageHorizontalInset * 2
                height: accountContent.height - accountContent.pageTopInset - accountContent.pageBottomInset
                active: root.visible && root.activeSection === "security"
                visible: active
                controller: root.controller
            }

            AccountDevicesPage {
                x: accountContent.pageHorizontalInset
                y: accountContent.pageTopInset
                width: accountContent.width - accountContent.pageHorizontalInset * 2
                height: accountContent.height - accountContent.pageTopInset - accountContent.pageBottomInset
                active: root.visible && root.activeSection === "devices"
                visible: active
                controller: root.controller
            }

            AccountRecoveryPage {
                x: accountContent.pageHorizontalInset
                y: accountContent.pageTopInset
                width: accountContent.width - accountContent.pageHorizontalInset * 2
                height: accountContent.height - accountContent.pageTopInset - accountContent.pageBottomInset
                active: root.visible && root.activeSection === "recovery"
                visible: active
                controller: root.controller
                presenter: root.recoveryPresenter
            }

            AccountDataPrivacyPage {
                id: privacyPage
                x: accountContent.pageHorizontalInset
                y: accountContent.pageTopInset
                width: parent.width - (root.compactPhoneLayout
                    ? accountContent.pageHorizontalInset * 2 : 86)
                height: accountContent.height - accountContent.pageTopInset - accountContent.pageBottomInset
                active: root.visible && root.activeSection === "privacy"
                visible: active

                rememberSearchHistory: root.privacyRememberSearchHistory
                keepActivityHistory: root.privacyKeepActivityHistory
                syncActivityHistory: root.privacySyncActivityHistory
                rememberSearchHistoryBusy: root.privacyRememberSearchHistoryBusy
                keepActivityHistoryBusy: root.privacyKeepActivityHistoryBusy
                syncActivityHistoryBusy: root.privacySyncActivityHistoryBusy
                clearSearchHistoryBusy: root.privacyClearSearchHistoryBusy
                clearActivityHistoryBusy: root.privacyClearActivityHistoryBusy
                dataExportBusy: root.privacyDataExportBusy
                accountDeletionFlowBusy: root.privacyAccountDeletionFlowBusy
                errorMessage: root.privacyErrorMessage
                statusMessage: root.privacyStatusMessage

                onRememberSearchHistoryChangeRequested: function(enabled) {
                    root.privacyRememberSearchHistoryChangeRequested(enabled)
                }
                onKeepActivityHistoryChangeRequested: function(enabled) {
                    root.privacyKeepActivityHistoryChangeRequested(enabled)
                }
                onSyncActivityHistoryChangeRequested: function(enabled) {
                    root.privacySyncActivityHistoryChangeRequested(enabled)
                }
                onClearSearchHistoryRequested:
                    root.privacyClearSearchHistoryRequested()
                onClearActivityHistoryRequested:
                    root.privacyClearActivityHistoryRequested()
                onDataExportRequested:
                    root.privacyDataExportRequested()
                onDataExportCancelRequested:
                    root.privacyDataExportCancelRequested()
                onAccountDeletionFlowRequested:
                    function(currentPassword) {
                        root.privacyAccountDeletionFlowRequested(currentPassword)
                    }
            }
        }
    }
}
