// AccountCenter.qml
// Composed Account Centre candidate against the current live Colosseum shell.
// All six locked page packages coexist here so transplantation does not depend
// on stacking mutually-overlapping AccountCenter patches.

import QtQuick
import QtQuick.Controls
import ".."
import "../AccountActivityFormat.js" as AccountActivityFormat

Rectangle {
    id: root
    objectName: "accountCenter"

    property var controller: null
    property var recoveryPresenter: null
    readonly property bool accountPresent: controller
        && (controller.mode === "signedIn" || controller.mode === "offline")
    readonly property bool onlineAccount: controller
        && controller.mode === "signedIn"
    property string initial: "?"
    property string activeSection: "colosseum"

    // Your Colosseum: bound to the native ProfileActivity projection (CPP-PORT-CONTRACT.md
    // arcs/02-profile-account-centre/activity-engine/reference, section 14 "QML model
    // contract"). This host owns selected-month UI state and formats ONE raw projection into
    // AccountYourColosseumPage.qml's existing presentation-only seam; the page itself gained no
    // aggregation logic (section 14: "Do not move aggregation into
    // AccountYourColosseumPage.qml").

    // Selected month, "YYYY-MM" — UI state, reset to the current system-local month each time
    // the Centre opens from closed (see open() below), never a live clock binding (section 12).
    property string colosseumMonthKey: ""
    // The "current month" navigation ceiling, frozen at the same open() moment as
    // colosseumMonthKey (section 12: "next month enabled only while selected month is before
    // current month" — a live re-read of today's date here would make next/previous flicker
    // mid-session if the Centre is left open across a real month boundary).
    property string colosseumCurrentMonthKey: ""
    // Section 12: "previous month stops at ProfileActivity.earliestActivityMonth() when one
    // exists" — recomputed on revision too, since new activity can move the earliest month.
    property string colosseumEarliestMonthKey: {
        if (typeof ProfileActivity === "undefined" || !ProfileActivity)
            return ""
        void ProfileActivity.revision
        return ProfileActivity.earliestActivityMonth()
    }

    // Section 14 "Recommended binding": exactly one projectMonth() call per selected
    // month/revision (section 24 performance rule) — every yourColosseum* metric below reads
    // THIS cached object, never calls projectMonth() itself. The null-guard/revision-read/
    // projectMonth-call algorithm lives once in AccountActivityFormat.projectionFor() (not
    // copy-pasted here) so tst_account_activity_binding.qml can prove the exact-one-call rule
    // against a recording fake without instantiating this whole composed host.
    property var colosseumProjection: AccountActivityFormat.projectionFor(
            typeof ProfileActivity !== "undefined" ? ProfileActivity : null,
            colosseumMonthKey)

    property string yourColosseumMonthName: AccountActivityFormat.monthName(colosseumMonthKey)
    property string yourColosseumMonthYear: AccountActivityFormat.monthYear(colosseumMonthKey)
    property string yourColosseumWatchTimeText:
        AccountActivityFormat.durationText(colosseumProjection.watchSeconds)
    property string yourColosseumPagesReadText:
        AccountActivityFormat.countText(colosseumProjection.pagesRead)
    property string yourColosseumCompletedText:
        AccountActivityFormat.countText(colosseumProjection.completedCount)
    property string yourColosseumActiveDaysText:
        AccountActivityFormat.countText(colosseumProjection.activeDays)
    property var yourColosseumHighlights:
        AccountActivityFormat.formatHighlights(colosseumProjection.highlights)
    property var yourColosseumRecentActivity:
        AccountActivityFormat.formatRecentActivity(colosseumProjection.recentActivity)
    property bool yourColosseumPreviousMonthEnabled:
        AccountActivityFormat.previousMonthEnabled(colosseumMonthKey, colosseumEarliestMonthKey)
    property bool yourColosseumNextMonthEnabled:
        AccountActivityFormat.nextMonthEnabled(colosseumMonthKey, colosseumCurrentMonthKey)

    signal yourColosseumPreviousMonthRequested()
    signal yourColosseumNextMonthRequested()

    // Mutates the host's own selected-month UI state; AccountYourColosseumPage.qml stays
    // presentation-only and only ever emits the request.
    onYourColosseumPreviousMonthRequested: {
        if (yourColosseumPreviousMonthEnabled)
            colosseumMonthKey = AccountActivityFormat.shiftMonthKey(colosseumMonthKey, -1)
    }
    onYourColosseumNextMonthRequested: {
        if (yourColosseumNextMonthEnabled)
            colosseumMonthKey = AccountActivityFormat.shiftMonthKey(colosseumMonthKey, 1)
    }

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

    signal privacyRememberSearchHistoryChangeRequested(bool enabled)
    signal privacyKeepActivityHistoryChangeRequested(bool enabled)
    signal privacySyncActivityHistoryChangeRequested(bool enabled)
    signal privacyClearSearchHistoryRequested()
    signal privacyClearActivityHistoryRequested()
    signal privacyDataExportRequested()
    signal privacyAccountDeletionFlowRequested()

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
    readonly property var privacySearchHistoryScopes: ["biblio", "tankoban", "theatre"]

    onPrivacyRememberSearchHistoryChangeRequested: function(enabled) {
        if (searchHistoryStore)
            searchHistoryStore.rememberEnabled = enabled
    }

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
    onVisibleChanged: opacity = visible ? 1 : 0

    function open(section) {
        // Section 12/14: selected month resets to "current system-local month" once per
        // closed->open transition, not on every internal rail-tab switch while already open.
        if (!root.visible) {
            colosseumMonthKey = AccountActivityFormat.currentMonthKey()
            colosseumCurrentMonthKey = colosseumMonthKey
        }
        // Preserve old flyout callers while Your library finishes migrating to
        // the locked Your Colosseum destination.
        if (section === "library")
            activeSection = "colosseum"
        else if (section)
            activeSection = section
        root.visible = true
    }

    function close() {
        root.visible = false
    }

    Keys.onEscapePressed: root.close()

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
            width: 232
            height: parent.height
            color: "#121009"

            Column {
                x: 20
                y: 74
                width: parent.width - 40
                spacing: 4

                Row {
                    spacing: 12
                    bottomPadding: 18

                    Rectangle {
                        width: 38
                        height: 38
                        radius: 19
                        color: Qt.rgba(0.94, 0.77, 0.29, 0.14)
                        border.width: 1.5
                        border.color: Qt.rgba(0.94, 0.77, 0.29, 0.75)

                        Text {
                            anchors.centerIn: parent
                            text: root.accountPresent ? root.initial : "?"
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
                            text: root.accountPresent && root.controller
                                ? root.controller.username
                                : qsTr("Not signed in")
                            color: "#f2f2ef"
                            font.family: "Inter"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: qsTr("Colosseum account")
                            color: "#7d7a6f"
                            font.family: "Inter"
                            font.pixelSize: 10
                        }
                    }
                }

                Repeater {
                    model: [
                        { id: "profile", label: qsTr("Profile"), glyph: "◌" },
                        { id: "colosseum", label: qsTr("Your Colosseum"), glyph: "▥" },
                        { id: "security", label: qsTr("Security"), glyph: "◇" },
                        { id: "devices", label: qsTr("Devices"), glyph: "▣" },
                        { id: "recovery", label: qsTr("Recovery"), glyph: "↶" },
                        { id: "privacy", label: qsTr("Data & privacy"), glyph: "◫" }
                    ]

                    Rectangle {
                        objectName: "accountCenterRail_" + modelData.id
                        width: parent ? parent.width : 0
                        height: 38
                        radius: 9
                        color: root.activeSection === modelData.id
                            ? Qt.rgba(0.94, 0.77, 0.29, 0.12)
                            : (railMa.containsMouse
                                ? Qt.rgba(1, 1, 1, 0.05)
                                : "transparent")

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
                    text: root.accountPresent ? qsTr("Sign out") : qsTr("Sign in")
                    color: railOutMa.containsMouse ? "#d8d4c8" : "#8f8b80"
                    font.family: "Inter"
                    font.pixelSize: 12

                    MouseArea {
                        id: railOutMa
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.controller) {
                                if (root.accountPresent)
                                    root.controller.logoutCurrent()
                                else
                                    root.controller.returnToSignIn()
                            }
                            root.close()
                        }
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
            width: parent.width - 232
            height: parent.height

            Text {
                x: 34
                y: 30
                text: qsTr("‹ Back")
                color: backMa.containsMouse ? "#b7b3a6" : "#8f8b80"
                font.family: "Inter"
                font.pixelSize: 12

                MouseArea {
                    id: backMa
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }

            AccountProfilePage {
                x: 34
                y: 64
                width: parent.width - 68
                height: parent.height - 96
                active: root.visible && root.activeSection === "profile"
                visible: active
                controller: root.controller
            }

            AccountYourColosseumPage {
                x: 34
                y: 64
                width: parent.width - 68
                height: parent.height - 96
                active: root.visible && root.activeSection === "colosseum"
                visible: active

                monthName: root.yourColosseumMonthName
                monthYear: root.yourColosseumMonthYear
                watchTimeText: root.yourColosseumWatchTimeText
                pagesReadText: root.yourColosseumPagesReadText
                completedText: root.yourColosseumCompletedText
                activeDaysText: root.yourColosseumActiveDaysText
                highlights: root.yourColosseumHighlights
                recentActivity: root.yourColosseumRecentActivity
                previousMonthEnabled: root.yourColosseumPreviousMonthEnabled
                nextMonthEnabled: root.yourColosseumNextMonthEnabled

                onPreviousMonthRequested:
                    root.yourColosseumPreviousMonthRequested()
                onNextMonthRequested:
                    root.yourColosseumNextMonthRequested()
            }

            AccountSecurityPage {
                x: 34
                y: 64
                width: parent.width - 68
                height: parent.height - 96
                active: root.visible && root.activeSection === "security"
                visible: active
                controller: root.controller
            }

            AccountDevicesPage {
                x: 34
                y: 64
                width: parent.width - 68
                height: parent.height - 96
                active: root.visible && root.activeSection === "devices"
                visible: active
                controller: root.controller
            }

            AccountRecoveryPage {
                x: 34
                y: 64
                width: parent.width - 68
                height: parent.height - 96
                active: root.visible && root.activeSection === "recovery"
                visible: active
                controller: root.controller
                presenter: root.recoveryPresenter
            }

            AccountDataPrivacyPage {
                id: privacyPage
                x: 34
                y: 64
                width: parent.width - 86
                height: parent.height - 96
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
                onAccountDeletionFlowRequested:
                    root.privacyAccountDeletionFlowRequested()
            }
        }
    }
}
