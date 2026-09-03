from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


EXPECT = {
    "qml/account/AccountCenter.qml": [
        'objectName: "accountCenterRailRegion"',
        "SystemFocusContainment.move",
        "function restoreInvoker()",
        "Keys.onPressed: function(event)",
    ],
    "qml/account/AccountFlyout.qml": [
        "function openCentre(section)",
        "accountCenter.open(section, invoker)",
        "function restoreInvoker()",
        "SystemFocusContainment.move",
    ],
    "qml/account/AccountProfilePage.qml": [
        "function handleAvatarKey(event, index)",
        "Qt.Key_Home",
        "Qt.Key_End",
    ],
    "qml/account/AccountDevicesPage.qml": [
        "property string keyboardFocusedDeviceId",
        "function restoreTrackedDeviceFocus()",
        "deviceRevoke_",
        "Qt.Key_Escape",
    ],
    "qml/account/AccountSecurityPage.qml": [
        "function openPasswordEditor()",
        "function closeLogoutConfirmation(restoreFocus)",
        "KeyboardScrollController",
    ],
    "qml/account/AccountRecoveryPage.qml": [
        "root.cancelReplacement()",
        "KeyboardScrollController",
    ],
    "qml/account/AccountDataPrivacyPage.qml": [
        "root.closeDeleteConfirmation()",
        "root.closeActivityClearConfirmation()",
        "root.closeSearchClearConfirmation()",
    ],
    "qml/WatchPartyJoinSheet.qml": [
        "property Item focusReturnItem",
        "onOpened: focusInitial()",
        "onClosed: restoreInvoker()",
        "SystemFocusContainment.move",
    ],
    "qml/WatchPartyPanel.qml": [
        "bodyKeyboardScroll",
        "chatKeyboardScroll",
        "SystemFocusContainment.move",
        "party.closeFromPanel()",
    ],
    "qml/UpdatePage.qml": [
        "activeFocusOnTab: visible && enabled",
        'objectName: "colosseumUpdateMinimize"',
        'objectName: "colosseumUpdateFullscreen"',
        'objectName: "colosseumUpdateClose"',
    ],
    "qml/update/UpdateLivingGallery.qml": [
        "activeFocusOnTab: root.chapterCount > 1",
        "event.accepted = root.moveChapter(-1)",
        "event.accepted = root.moveChapterTo(0)",
        "activeFocusOnTab: root.chapterCount >= 2",
    ],
}


def test_account_system_keyboard_contract_markers():
    for relative, markers in EXPECT.items():
        text = (ROOT / relative).read_text(encoding="utf-8-sig")
        for marker in markers:
            assert marker in text, f"{relative}: missing {marker!r}"


if __name__ == "__main__":
    test_account_system_keyboard_contract_markers()
    print("K06_ACCOUNT_SYSTEM_KEYBOARD_CONTRACT_OK")
