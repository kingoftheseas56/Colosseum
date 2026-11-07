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
        property var profiles: [{index: 0, name: "Locked", locked: true},
                                {index: 3, name: "Main", locked: false}]
        property var preview: [{category: "library", matched: 1, unresolved: 2, existing: 3, skipped: 4}]
        property string signedInEmail: ""
        property int previewedProfile: -1
        property bool previewAsPrimary: false
        property bool applyAsPrimary: false
        property int cancels: 0

        function signIn(email, password) {
            signedInEmail = email
            phase = "profiles"
        }

        function previewProfile(index, firstRun) {
            previewedProfile = index
            previewAsPrimary = firstRun
            phase = "preview"
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

        function prepareTheatreFirstRun() {
            accountFixture.available = true
            return true
        }

        function continueWithoutAccount() { continued++ }
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

    Colosseum.NuvioAccountFlow {
        id: flow
        width: 300
        height: 200
        account: accountFixture
        firstRun: true
    }

    SignalSpy { id: completedSpy; target: flow; signalName: "completed" }
    SignalSpy { id: canceledSpy; target: flow; signalName: "cancelRequested" }

    function init() {
        accountFixture.phase = "idle"
        accountFixture.available = true
        accountFixture.busy = false
        accountFixture.signedInEmail = ""
        accountFixture.previewedProfile = -1
        accountFixture.previewAsPrimary = false
        accountFixture.applyAsPrimary = false
        accountFixture.cancels = 0
        accountFixture.connected = false
        accountFixture.primaryProvider = ""
        stremioActionsFixture.stremioImportPhase = "idle"
        stremioActionsFixture.applyCalls = 0
        stremioActionsFixture.appliedAsFirstRun = false
        stremioActionsFixture.stremioImportBecomesPrimary = false
        onboardingController.continued = 0
        onboardingController.accountFeaturesAvailable = true
        onboarding.routeName = "welcome"
        stremioActionsFixture.syncCalls = 0
        syncPanel.shown = false
        trackerSyncSpy.clear()
        flow.clearSecrets()
        completedSpy.clear()
        canceledSpy.clear()
    }

    function test_first_run_requires_unlocked_profile_and_explicit_apply() {
        const email = findChild(flow, "nuvioEmail")
        const password = findChild(flow, "nuvioPassword")
        email.text = " reader@example.invalid "
        password.text = "fixture-password"
        findChild(flow, "nuvioSignIn").clicked()
        compare(accountFixture.signedInEmail, "reader@example.invalid")
        compare(password.text, "")
        compare(email.text, "")
        compare(findChild(flow, "nuvioProfile_0").enabled, false)

        findChild(flow, "nuvioProfile_3").clicked()
        compare(accountFixture.previewedProfile, 3)
        compare(accountFixture.previewAsPrimary, true)
        compare(completedSpy.count, 0)
        findChild(flow, "nuvioApplyImport").clicked()
        compare(accountFixture.applyAsPrimary, true)
        accountFixture.phase = "done"
        compare(completedSpy.count, 0)
        accountFixture.busy = false
        compare(completedSpy.count, 1)
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
