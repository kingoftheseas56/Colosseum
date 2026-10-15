// Composed Account Centre integration regression.
// This test exists specifically to catch cross-page host wiring defects that
// isolated page tests cannot see.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountCenterComposed"

    Window {
        id: testWindow
        width: 1280
        height: 900
        visible: true
    }

    Component {
        id: presenterComponent
        QtObject {
            property bool active: false
            property string purpose: ""
            property string recoveryKey: ""
            property string copyState: "idle"
        }
    }

    Component {
        id: centerComponent
        Account.AccountCenter {}
    }

    property var presenter: null
    property var center: null

    function byName(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = byName(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function findText(root, value) {
        if (!root)
            return null
        if (root.text !== undefined && root.text === value)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findText(children[i], value)
            if (found)
                return found
        }
        return null
    }

    function init() {
        presenter = presenterComponent.createObject(testWindow)
        verify(presenter !== null)
        center = centerComponent.createObject(testWindow.contentItem, {
            "width": 1280,
            "height": 900,
            "recoveryPresenter": presenter,
            "yourColosseumMonthName": "ComposedMonthMarker"
        })
        verify(center !== null)
        wait(0)
    }

    function cleanup() {
        if (center)
            center.destroy()
        if (presenter)
            presenter.destroy()
        center = null
        presenter = null
    }

    function test_legacy_library_route_maps_to_your_colosseum() {
        center.open("library")
        wait(0)
        compare(center.activeSection, "colosseum")
        verify(findText(center, "ComposedMonthMarker") !== null)
    }

    function test_all_six_locked_routes_instantiate_in_one_host() {
        center.open("profile")
        wait(0)
        compare(center.activeSection, "profile")
        verify(byName(center, "accountProfileUsername") !== null)

        center.open("colosseum")
        wait(0)
        compare(center.activeSection, "colosseum")
        verify(findText(center, "ComposedMonthMarker") !== null)

        center.open("security")
        wait(0)
        compare(center.activeSection, "security")
        verify(byName(center, "accountSecurityPage") !== null)
        verify(byName(center, "accountSecurityPage").visible)

        center.open("devices")
        wait(0)
        compare(center.activeSection, "devices")
        verify(byName(center, "accountDevicesPage") !== null)
        verify(byName(center, "accountDevicesPage").visible)

        center.open("recovery")
        wait(0)
        compare(center.activeSection, "recovery")
        var recovery = byName(center, "accountRecoveryPage")
        verify(recovery !== null)
        verify(recovery.visible)
        compare(recovery.presenter, presenter)

        center.open("privacy")
        wait(0)
        compare(center.activeSection, "privacy")
        verify(byName(center, "accountDataPrivacyPage") !== null)
        verify(byName(center, "accountDataPrivacyPage").visible)
    }

    function test_switching_sections_clears_recovery_transient_secret_input() {
        center.open("recovery")
        wait(0)
        var recovery = byName(center, "accountRecoveryPage")
        verify(recovery !== null)
        recovery.beginReplacement()
        var password = byName(recovery, "recoveryCurrentPassword")
        verify(password !== null)
        password.text = "composed-route-secret-sentinel"

        center.open("security")
        wait(0)

        compare(password.text, "")
        compare(recovery.replaceExpanded, false)
    }

    function test_close_and_reopen_resyncs_profile_draft() {
        center.open("profile")
        wait(0)
        var profilePage = byName(center, "accountProfilePage")
        verify(profilePage !== null)

        var field = byName(center, "accountProfileUsername")
        verify(field !== null)
        field.text = "Dirty Draft Sentinel"
        compare(profilePage.usernameDraft, "Dirty Draft Sentinel")

        center.close()
        wait(0)
        center.open("profile")
        wait(0)

        compare(profilePage.usernameDraft, profilePage.usernameBaseline)
        compare(profilePage.usernameRequestPending, false)
        compare(profilePage.avatarRequestPending, false)
        compare(profilePage.profileUpdated, false)
        compare(profilePage.operationErrorMessage, "")
    }
}
