// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

Item {
    id: root
    objectName: "accountOnboarding"

    property var controller: null
    property var recoveryPresenter: null
    property Item backdrop: null

    property string routeName: "welcome"
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
            || routeName === "deviceApproval") {
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
            root.synchronizeRoute(false)
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
            onSignInRequested: root.goSignIn()
            onCreateAccountRequested: root.goCreate()
        }
    }

    Component {
        id: signInComponent

        AccountSignIn {
            controller: root.controller
            backdrop: root.backdrop
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

}
