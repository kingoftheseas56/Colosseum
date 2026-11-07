// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

Item {
    id: root
    objectName: "accountOnboarding"

    property var controller: null
    property var recoveryPresenter: null
    property Item backdrop: null
    property var nuvioAccount: typeof NuvioAccounts !== "undefined" ? NuvioAccounts : null
    property var stremioActions: typeof StremioActions !== "undefined" ? StremioActions : null

    readonly property bool stremioFirstRunAvailable: stremioActions
        && typeof stremioActions.beginStremioFirstRun === "function"
        && typeof stremioActions.applyStremioImport === "function"
        && typeof stremioActions.cancelStremioImport === "function"
    readonly property bool nuvioAvailable: !!nuvioAccount && nuvioAccount.available
    readonly property bool stremioAvailable: controller && controller.onboardingRequired
        ? stremioFirstRunAvailable
        : !!stremioActions && typeof stremioActions.previewStremioImport === "function"
    readonly property bool theatreSyncAvailable: controller && controllerMode === "signedOut"
        && controller.accountFeaturesAvailable !== false
        && (stremioAvailable || nuvioAvailable)

    property string routeName: "welcome"
    property string theatreReturnRoute: "welcome"
    signal theatreSyncSettingsRequested()
    readonly property bool presenterActive:
        recoveryPresenter ? recoveryPresenter.active === true : false
    readonly property string controllerMode:
        controller ? controller.mode : "signedOut"

    function baseRoute() {
        if (!controller)
            return "welcome"

        if (controller.mode === "awaitingDeviceApproval")
            return "deviceApproval"

        if (controller.mode === "locked")
            return "signIn"

        // No-account edition: storage failures route to the local recovery
        // surface regardless of onboarding completion — a returning user
        // must never be dropped onto the sign-in route by an error, and the
        // error/retry pair is the only thing reachable from there.
        if (controller.mode === "error"
            && controller.accountFeaturesAvailable === false)
            return "localRecovery"

        if (controller.mode === "signedOut"
            || controller.mode === "authenticating"
            || controller.mode === "error") {
            return controller.onboardingRequired ? "welcome" : "signIn"
        }

        return routeName
    }

    function synchronizeRoute(force) {
        if (presenterActive) {
            routeName = "recoveryKey"
            return
        }

        if (controllerMode === "awaitingDeviceApproval") {
            routeName = "deviceApproval"
            return
        }

        if (force
            || routeName === "recoveryKey"
            || routeName === "deviceApproval"
            // An error must always re-resolve its route: the no-account
            // edition routes storage failures to localRecovery, and the
            // standard edition re-resolves to welcome/signIn as before.
            || controllerMode === "error") {
            routeName = baseRoute()
        }
    }

    function goSignIn() {
        routeName = "signIn"
    }

    function goCreate() {
        routeName = "create"
    }

    function goRecovery() {
        routeName = "recovery"
    }

    function goTheatreSync() {
        if (!theatreSyncAvailable)
            return

        if (controller.onboardingRequired) {
            if (controller.prepareTheatreFirstRun()) {
                theatreReturnRoute = routeName
                routeName = "theatreSync"
            }
            return
        }

        // Returning users already have a profile. Open its regular sync settings
        // so adding a service doesn't repeat first-run setup or make it primary.
        controller.continueWithoutAccount()
        if (controller.mode === "localOnly")
            theatreSyncSettingsRequested()
    }

    Component.onCompleted: synchronizeRoute(true)

    function clearCurrentSecrets() {
        if (!pageLoader.item)
            return
        if (typeof pageLoader.item.clearSecrets === "function")
            pageLoader.item.clearSecrets()
    }

    Connections {
        target: root.controller
        function onModeChanged() {
            if (root.controller
                && (root.controller.mode === "signedIn"
                    || root.controller.mode === "localOnly"
                    || root.controller.mode === "awaitingDeviceApproval"
                    || root.controller.mode === "awaitingRecoveryApproval")) {
                root.clearCurrentSecrets()
            }
            const returningToSignIn = root.controller && root.controller.mode === "signedOut"
                && !root.controller.onboardingRequired
            root.synchronizeRoute(returningToSignIn)
        }
        function onOnboardingRequiredChanged() {
            if (root.routeName === "welcome" || root.routeName === "signIn")
                root.synchronizeRoute(true)
        }
    }

    Connections {
        target: root.recoveryPresenter
        function onActiveChanged() {
            root.synchronizeRoute(true)
        }
    }

    Loader {
        id: pageLoader
        anchors.fill: parent
        active: true
        sourceComponent: {
            switch (root.routeName) {
            case "theatreSync":
                return theatreSyncComponent
            case "signIn":
                return signInComponent
            case "create":
                return createComponent
            case "recovery":
                return recoveryComponent
            case "recoveryKey":
                return recoveryKeyComponent
            case "deviceApproval":
                return deviceApprovalComponent
            case "localRecovery":
                return localRecoveryComponent
            default:
                return welcomeComponent
            }
        }
    }

    Component {
        id: welcomeComponent

        AccountWelcome {
            controller: root.controller
            backdrop: root.backdrop
            theatreSyncAvailable: root.theatreSyncAvailable
            onTheatreSyncRequested: root.goTheatreSync()
            onSignInRequested: root.goSignIn()
            onCreateAccountRequested: root.goCreate()
        }
    }

    Component {
        id: theatreSyncComponent

        AccountTheatreSync {
            backdrop: root.backdrop
            nuvioAccount: root.nuvioAccount
            stremioActions: root.stremioActions
            stremioAvailable: root.stremioFirstRunAvailable
            cancelDetail: root.theatreReturnRoute === "signIn"
                ? qsTr("Return to Colosseum sign-in.") : qsTr("Return to the welcome screen.")
            onCancelRequested: {
                clearSecrets()
                root.routeName = root.theatreReturnRoute
            }
            onCompleted: {
                clearSecrets()
                root.routeName = "welcome"
                if (root.controller)
                    root.controller.continueWithoutAccount()
            }
        }
    }

    Component {
        id: signInComponent

        AccountSignIn {
            controller: root.controller
            backdrop: root.backdrop
            stremioAvailable: root.theatreSyncAvailable && root.stremioAvailable
            nuvioAvailable: root.theatreSyncAvailable && root.nuvioAvailable
            onTheatreSyncRequested: root.goTheatreSync()
            onForgotPasswordRequested: root.goRecovery()
            onCreateAccountRequested: root.goCreate()
        }
    }

    Component {
        id: createComponent

        AccountCreate {
            controller: root.controller
            backdrop: root.backdrop
            onSignInRequested: root.goSignIn()
        }
    }

    Component {
        id: recoveryComponent

        AccountRecovery {
            controller: root.controller
            backdrop: root.backdrop
            onSignInRequested: root.goSignIn()
        }
    }

    Component {
        id: recoveryKeyComponent

        AccountRecoveryKey {
            presenter: root.recoveryPresenter
            backdrop: root.backdrop

            onFinished: function(purpose) {
                if (purpose === "passwordRecovered")
                    root.goSignIn()
                else
                    root.synchronizeRoute(true)
            }
        }
    }

    Component {
        id: deviceApprovalComponent

        AccountDeviceApproval {
            controller: root.controller
            backdrop: root.backdrop
            onSignInRequested: root.goSignIn()
        }
    }

    Component {
        id: localRecoveryComponent

        AccountLocalRecovery {
            controller: root.controller
            backdrop: root.backdrop
        }
    }

}
