import QtQuick
import QtTest
import "../../qml" as Colosseum
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "NuvioAccountFlow"
    when: windowShown
    visible: true

    QtObject {
        id: accountFixture
        property bool available: true
        property bool connected: false
        property bool busy: false
        property bool needsReconnect: false
        property string phase: "idle"
        property string message: ""
        property string primaryProvider: ""
        property string selectedProfileName: ""
        property string connectedProfileName: ""
        property var profiles: [{index: 0, name: "<b>Family</b>", locked: true,
                                 current: connectedProfileName === "<b>Family</b>"},
                                {index: 3, name: "Main", locked: false,
                                 current: connectedProfileName === "Main"}]
        property var preview: [{category: "library", matched: 1, unresolved: 2, existing: 3, skipped: 4}]
        property string signedInEmail: ""
        property int previewedProfile: -1
        property bool previewAsPrimary: false
        property bool applyAsPrimary: false
        property int cancels: 0
        property string submittedPin: ""
        property int pinSubmissions: 0
        property int pinCancels: 0
        property string deviceUserCode: ""
        property int deviceStarts: 0
        property int deviceCancels: 0

        function signIn(email, password) {
            signedInEmail = email
            phase = "profiles"
        }

        function startDeviceSignIn() {
            deviceStarts++
            deviceUserCode = ""
            phase = "deviceCode"
            busy = true
        }

        function cancelDeviceSignIn() {
            deviceCancels++
            deviceUserCode = ""
            phase = "signIn"
            busy = false
        }

        function previewProfile(index, firstRun) {
            previewedProfile = index
            previewAsPrimary = firstRun
            selectedProfileName = profiles.find(profile => profile.index === index).name
            phase = index === 0 ? "pin" : "preview"
        }

        function unlockProfile(pin) {
            submittedPin = pin
            pinSubmissions++
            busy = true
        }

        function cancelPin() {
            pinCancels++
            busy = false
            phase = "profiles"
        }

        function apply(firstRun) {
            applyAsPrimary = firstRun
            busy = true
        }

        function cancel() {
            cancels++
            phase = "idle"
        }
    }

    QtObject {
        id: stremioFixture
        property string status: "synced"
        property bool linkedAccount: true
    }

    QtObject {
        id: stremioActionsFixture
        property string stremioImportPhase: "idle"
        property var stremioImportPreview: accountFixture.preview
        property int syncCalls: 0
        property int applyCalls: 0
        property bool appliedAsFirstRun: false
        property bool stremioImportBecomesPrimary: false

        function beginStremioFirstRun() { stremioImportPhase = "loading" }
        function syncStremioNow() { syncCalls++ }
        function previewStremioImport() { stremioImportPhase = "preview" }
        function cancelStremioImport() {
            stremioImportPhase = "idle"
            stremioImportBecomesPrimary = false
        }
        function applyStremioImport(firstRun) {
            applyCalls++
            appliedAsFirstRun = firstRun === true
            if (stremioImportBecomesPrimary)
                accountFixture.primaryProvider = "stremio"
            stremioImportBecomesPrimary = false
            stremioImportPhase = appliedAsFirstRun ? "applying" : "idle"
        }
        function makeStremioPrimary() {
            stremioImportBecomesPrimary = true
            stremioImportPhase = "preview"
        }
    }

    Colosseum.StremioSyncPanel {
        id: syncPanel
        width: 480
        height: 400
        shown: false
        nuvioAccount: accountFixture
        syncState: stremioFixture
        actions: stremioActionsFixture
    }

    Colosseum.TrackerSyncCenterPage {
        id: trackerPage
        width: 540
        height: 500
        nuvioAccount: accountFixture
        stremioState: stremioFixture
    }

    QtObject {
        id: onboardingController
        property string mode: "signedOut"
        property bool onboardingRequired: true
        property bool accountFeaturesAvailable: true
        property int continued: 0
        property int prepareCalls: 0
        property string lastErrorMessage: ""

        function prepareTheatreFirstRun() {
            prepareCalls++
            accountFixture.available = true
            return true
        }

        function returnToSignIn() { mode = "signedOut" }

        function continueWithoutAccount() {
            continued++
            if (!onboardingRequired)
                mode = "localOnly"
        }
    }

    Account.AccountOnboarding {
        id: onboarding
        width: 1000
        height: 620
        controller: onboardingController
        nuvioAccount: accountFixture
        stremioActions: stremioActionsFixture
    }

    SignalSpy { id: trackerSyncSpy; target: trackerPage; signalName: "mainSyncRequested" }
    SignalSpy { id: returningSyncSpy; target: onboarding; signalName: "theatreSyncSettingsRequested" }

    Colosseum.NuvioAccountFlow {
        id: flow
        width: 300
        height: 200
        account: accountFixture
        firstRun: true
    }

    SignalSpy { id: completedSpy; target: flow; signalName: "completed" }
    SignalSpy { id: canceledSpy; target: flow; signalName: "cancelRequested" }

    Shortcut {
        sequences: ["Escape", "Back"]
        enabled: accountFixture.phase === "deviceCode" || accountFixture.phase === "deviceCodeExpired"
        onActivated: accountFixture.cancels++
    }

    function init() {
        accountFixture.phase = "idle"
        accountFixture.available = true
        accountFixture.busy = false
        accountFixture.signedInEmail = ""
        accountFixture.previewedProfile = -1
        accountFixture.previewAsPrimary = false
        accountFixture.applyAsPrimary = false
        accountFixture.cancels = 0
        accountFixture.submittedPin = ""
        accountFixture.pinSubmissions = 0
        accountFixture.pinCancels = 0
        accountFixture.deviceUserCode = ""
        accountFixture.deviceStarts = 0
        accountFixture.deviceCancels = 0
        accountFixture.connected = false
        accountFixture.selectedProfileName = ""
        accountFixture.connectedProfileName = ""
        accountFixture.primaryProvider = ""
        stremioActionsFixture.stremioImportPhase = "idle"
        stremioActionsFixture.applyCalls = 0
        stremioActionsFixture.appliedAsFirstRun = false
        stremioActionsFixture.stremioImportBecomesPrimary = false
        onboardingController.continued = 0
        onboardingController.prepareCalls = 0
        onboardingController.mode = "signedOut"
        onboardingController.onboardingRequired = true
        onboardingController.accountFeaturesAvailable = true
        returningSyncSpy.clear()
        onboarding.routeName = "welcome"
        stremioActionsFixture.syncCalls = 0
        syncPanel.shown = false
        trackerSyncSpy.clear()
        flow.visible = true
        flow.clearSecrets()
        completedSpy.clear()
        canceledSpy.clear()
    }

    function test_first_run_unlocked_profile_requires_explicit_apply() {
        const email = findChild(flow, "nuvioEmail")
        const password = findChild(flow, "nuvioPassword")
        email.text = " reader@example.invalid "
        password.text = "fixture-password"
        findChild(flow, "nuvioSignIn").clicked()
        compare(accountFixture.signedInEmail, "reader@example.invalid")
        compare(password.text, "")
        compare(email.text, "")

        findChild(flow, "nuvioProfile_3").clicked()
        compare(accountFixture.previewedProfile, 3)
        compare(accountFixture.previewAsPrimary, true)
        const description = findChild(flow, "nuvioFlowDescription")
        verify(description && description.visible)
        compare(description.text, "Review import from Main")
        compare(completedSpy.count, 0)
        findChild(flow, "nuvioApplyImport").clicked()
        compare(accountFixture.applyAsPrimary, true)
        accountFixture.connected = true
        accountFixture.connectedProfileName = "Main"
        accountFixture.primaryProvider = "nuvio"
        accountFixture.phase = "done"
        compare(completedSpy.count, 0)
        accountFixture.busy = false
        compare(completedSpy.count, 1)
        compare(description.text, "Connected to Main · Primary sync service")
    }

    function test_locked_profile_requires_explicit_valid_pin_submission() {
        accountFixture.connected = true
        accountFixture.connectedProfileName = "Main"
        accountFixture.phase = "profiles"
        const lockedProfile = findChild(flow, "nuvioProfile_0")
        const currentProfile = findChild(flow, "nuvioProfile_3")
        compare(lockedProfile.text, "<b>Family</b> · Locked")
        compare(lockedProfile.contentItem.textFormat, Text.PlainText)
        compare(currentProfile.text, "Main · Current profile")
        verify(lockedProfile.enabled)
        lockedProfile.clicked()
        compare(accountFixture.previewedProfile, 0)
        verify(accountFixture.previewAsPrimary)
        compare(accountFixture.phase, "pin")
        const description = findChild(flow, "nuvioFlowDescription")
        verify(description && description.visible)
        compare(description.text, "Enter the four-digit PIN for <b>Family</b>.")
        compare(description.textFormat, Text.PlainText)

        const pin = findChild(flow, "nuvioPin")
        const unlock = findChild(flow, "nuvioUnlockProfile")
        verify(pin.visible)
        tryCompare(pin, "activeFocus", true)
        compare(pin.echoMode, TextInput.Password)
        verify((pin.inputMethodHints & Qt.ImhSensitiveData) !== 0)
        pin.text = "１２３４"
        verify(!unlock.enabled)
        unlock.clicked()
        compare(accountFixture.pinSubmissions, 0)

        pin.text = "0123"
        verify(unlock.enabled)
        compare(accountFixture.pinSubmissions, 0)
        keyClick(Qt.Key_Return)
        compare(accountFixture.pinSubmissions, 1)
        compare(accountFixture.submittedPin, "0123")
        compare(pin.text, "")
        pin.text = "4321"
        verify(!unlock.enabled)
        unlock.clicked()
        compare(accountFixture.pinSubmissions, 1)
        compare(completedSpy.count, 0)

        accountFixture.phase = "preview"
        accountFixture.busy = false
        compare(description.text, "Review import from <b>Family</b>")
        verify(findChild(flow, "nuvioApplyImport").visible)
        verify(!pin.visible)
        compare(pin.text, "")
        compare(completedSpy.count, 0)
    }

    function test_pin_back_cancels_pending_check_and_hiding_clears_input() {
        accountFixture.phase = "profiles"
        findChild(flow, "nuvioProfile_0").clicked()
        const pin = findChild(flow, "nuvioPin")
        pin.text = "0123"
        findChild(flow, "nuvioUnlockProfile").clicked()
        verify(accountFixture.busy)

        const back = findChild(flow, "nuvioPinBack")
        verify(back.enabled)
        back.clicked()
        compare(accountFixture.pinCancels, 1)
        compare(accountFixture.phase, "profiles")
        compare(canceledSpy.count, 0)
        compare(pin.text, "")

        findChild(flow, "nuvioProfile_0").clicked()
        pin.text = "0123"
        flow.visible = false
        compare(pin.text, "")
        flow.visible = true
        findChild(flow, "nuvioCancelImport").clicked()
        compare(accountFixture.cancels, 1)
        compare(canceledSpy.count, 1)
    }

    function test_device_sign_in_displays_code_and_continues_to_existing_import() {
        const email = findChild(flow, "nuvioEmail")
        const password = findChild(flow, "nuvioPassword")
        const start = findChild(flow, "nuvioStartDeviceSignIn")
        verify(email.visible && start && start.visible && start.enabled)
        email.text = "reader@example.invalid"
        password.text = "fixture-password"
        start.clicked()
        compare(accountFixture.deviceStarts, 1)
        compare(accountFixture.signedInEmail, "")
        verify(!email.visible)
        compare(email.text, "")
        compare(password.text, "")

        const code = findChild(flow, "nuvioDeviceUserCode")
        const link = findChild(flow, "nuvioDeviceLink")
        verify(!code.visible && !link.visible)
        accountFixture.deviceUserCode = "AB12CD"
        accountFixture.busy = false
        verify(code.visible && link.visible)
        compare(code.text, "AB12CD")
        compare(link.text, "nuvio.tv/link")
        tryVerify(() => code.width > 0 && link.width > 0)
        verify(code.width <= flow.availableWidth)
        verify(link.width <= flow.availableWidth)

        accountFixture.phase = "profiles"
        verify(!code.visible && !link.visible)
        findChild(flow, "nuvioProfile_3").clicked()
        compare(accountFixture.previewedProfile, 3)
        verify(accountFixture.previewAsPrimary)
        compare(completedSpy.count, 0)
        findChild(flow, "nuvioApplyImport").clicked()
        verify(accountFixture.applyAsPrimary)
        accountFixture.phase = "done"
        accountFixture.busy = false
        compare(completedSpy.count, 1)
    }

    function test_device_sign_in_retries_expired_code_and_returns_to_email() {
        findChild(flow, "nuvioStartDeviceSignIn").clicked()
        accountFixture.deviceUserCode = "AB12CD"
        accountFixture.busy = false
        accountFixture.phase = "deviceCodeExpired"
        verify(!findChild(flow, "nuvioDeviceUserCode").visible)
        verify(!findChild(flow, "nuvioDeviceLink").visible)

        const retry = findChild(flow, "nuvioRetryDeviceCode")
        verify(retry.visible && retry.enabled)
        retry.clicked()
        compare(accountFixture.deviceStarts, 2)
        compare(accountFixture.phase, "deviceCode")
        verify(!retry.visible)

        const back = findChild(flow, "nuvioDeviceSignInBack")
        verify(back.enabled)
        tryCompare(back, "activeFocus", true)
        keyClick(Qt.Key_Escape)
        compare(accountFixture.deviceCancels, 1)
        verify(findChild(flow, "nuvioEmail").visible)
        compare(accountFixture.cancels, 0)
        compare(canceledSpy.count, 0)

        findChild(flow, "nuvioStartDeviceSignIn").clicked()
        back.clicked()
        compare(accountFixture.deviceCancels, 2)
        compare(accountFixture.phase, "signIn")
        verify(findChild(flow, "nuvioEmail").visible)
        compare(canceledSpy.count, 0)

        findChild(flow, "nuvioStartDeviceSignIn").clicked()
        tryCompare(back, "activeFocus", true)
        keyClick(Qt.Key_Back)
        compare(accountFixture.deviceCancels, 3)
        compare(accountFixture.phase, "signIn")
        compare(canceledSpy.count, 0)
    }

    function test_cancel_clears_credentials_and_signals_route() {
        const email = findChild(flow, "nuvioEmail")
        const password = findChild(flow, "nuvioPassword")
        email.text = "reader@example.invalid"
        password.text = "fixture-password"
        findChild(flow, "nuvioCancelImport").clicked()
        compare(accountFixture.cancels, 1)
        compare(canceledSpy.count, 1)
        compare(email.text, "")
        compare(password.text, "")
    }

    function test_inactive_stremio_requires_import_confirmation() {
        accountFixture.primaryProvider = "nuvio"
        syncPanel.shown = true
        const stremioAction = findChild(syncPanel, "stremioSyncPrimaryAction")
        compare(stremioAction.visible, true)
        compare(stremioAction.label, "Preview import")
        stremioAction.triggered()
        compare(stremioActionsFixture.stremioImportPhase, "preview")
        compare(stremioActionsFixture.syncCalls, 0)
        compare(stremioActionsFixture.applyCalls, 0)
        compare(stremioAction.visible, false)
        findChild(syncPanel, "stremioCancelImport").triggered()
        compare(stremioActionsFixture.stremioImportPhase, "idle")
        stremioAction.triggered()
        findChild(syncPanel, "stremioApplyImport").triggered()
        compare(stremioActionsFixture.applyCalls, 1)
        findChild(syncPanel, "stremioMakePrimary").triggered()
        const apply = findChild(syncPanel, "stremioApplyImport")
        compare(apply.label, "Import and use Stremio as primary")
        compare(accountFixture.primaryProvider, "nuvio")
        apply.triggered()
        compare(stremioAction.label, "Sync now")
    }

    function test_tracker_shows_primary_service_and_opens_shared_settings() {
        accountFixture.connected = true
        accountFixture.primaryProvider = "nuvio"
        compare(trackerPage.nuvioStatusLabel(), "Primary sync service")
        compare(trackerPage.stremioStatusLabel(), "Connected")
        findChild(trackerPage, "trackerNuvioSettingsButton").clicked()
        compare(trackerSyncSpy.count, 1)
    }

    function test_onboarding_picker_cancels_and_completes_stremio() {
        accountFixture.available = false
        const choice = findChild(onboarding, "accountWelcomeTheatreSync")
        compare(onboarding.theatreSyncAvailable, true)
        compare(onboarding.stremioFirstRunAvailable, true)
        compare(choice.visible, true)
        choice.chosen()
        verify(findChild(onboarding, "accountTheatreChooseNuvio").visible)
        findChild(onboarding, "accountTheatrePickerCancel").chosen()
        compare(onboarding.routeName, "welcome")

        findChild(onboarding, "accountWelcomeTheatreSync").chosen()
        findChild(onboarding, "accountTheatreChooseStremio").chosen()
        const stremioSignIn = findChild(onboarding, "stremioSyncPrimaryAction")
        verify(stremioSignIn.visible)
        verify(stremioSignIn.height > 0)
        stremioSignIn.triggered()
        compare(stremioActionsFixture.stremioImportPhase, "loading")
        findChild(onboarding, "stremioCancelAction").triggered()
        compare(stremioActionsFixture.stremioImportPhase, "idle")
        compare(onboarding.routeName, "welcome")

        findChild(onboarding, "accountWelcomeTheatreSync").chosen()
        findChild(onboarding, "accountTheatreChooseStremio").chosen()
        findChild(onboarding, "stremioSyncPrimaryAction").triggered()
        stremioActionsFixture.stremioImportPhase = "preview"
        findChild(onboarding, "stremioApplyImport").triggered()
        compare(stremioActionsFixture.appliedAsFirstRun, true)
        compare(findChild(onboarding, "stremioCancelAction").enabled, false)
        compare(onboardingController.continued, 0)
        stremioActionsFixture.stremioImportPhase = "done"
        compare(onboardingController.continued, 1)
        compare(onboarding.routeName, "welcome")
    }

    function test_first_run_sign_in_can_open_provider_picker_and_cancel_back() {
        findChild(onboarding, "accountWelcomeSignIn").chosen()
        const theatre = findChild(onboarding, "accountSignInTheatreSync")
        verify(theatre && theatre.visible)
        findChild(onboarding, "accountSignInPassword").text = "temporary-fixture-password"
        theatre.chosen()
        verify(findChild(onboarding, "accountTheatreChooseStremio").visible)
        verify(findChild(onboarding, "accountTheatreChooseNuvio").visible)
        findChild(onboarding, "accountTheatrePickerCancel").chosen()
        compare(onboarding.routeName, "signIn")
        compare(findChild(onboarding, "accountSignInPassword").text, "")
        compare(onboardingController.continued, 0)
        compare(returningSyncSpy.count, 0)
    }

    function test_returning_sign_in_opens_settings_without_first_run() {
        onboardingController.mode = "localOnly"
        onboardingController.onboardingRequired = false
        compare(onboarding.routeName, "welcome")

        onboardingController.returnToSignIn()
        compare(onboarding.routeName, "signIn")
        findChild(onboarding, "accountSignInTheatreSync").chosen()
        compare(returningSyncSpy.count, 1)
        compare(onboardingController.mode, "localOnly")
        compare(onboardingController.prepareCalls, 0)
        compare(accountFixture.applyAsPrimary, false)
    }

    function test_onboarding_nuvio_confirms_before_continuing() {
        findChild(onboarding, "accountWelcomeTheatreSync").chosen()
        findChild(onboarding, "accountTheatreChooseNuvio").chosen()
        const email = findChild(onboarding, "nuvioEmail")
        const password = findChild(onboarding, "nuvioPassword")
        verify(email.visible)
        verify(email.height > 0)
        email.text = "reader@example.invalid"
        password.text = "fixture-password"
        findChild(onboarding, "nuvioSignIn").clicked()
        findChild(onboarding, "nuvioProfile_3").clicked()
        findChild(onboarding, "nuvioApplyImport").clicked()
        compare(findChild(onboarding, "nuvioCancelImport").enabled, false)
        compare(onboardingController.continued, 0)
        accountFixture.phase = "done"
        accountFixture.busy = false
        compare(onboardingController.continued, 1)
        compare(onboarding.routeName, "welcome")
    }

    function test_onboarding_hides_theatre_sync_for_local_only_edition() {
        onboardingController.accountFeaturesAvailable = false
        compare(findChild(onboarding, "accountWelcomeTheatreSync").visible, false)
    }
}
