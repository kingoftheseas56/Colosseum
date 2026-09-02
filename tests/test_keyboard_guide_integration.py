from pathlib import Path

root = Path(__file__).resolve().parents[1]
qml = root / "qml"
main = (qml / "Main.qml").read_text(encoding="utf-8")
taskbar = (qml / "Taskbar.qml").read_text(encoding="utf-8")
policy = (qml / "ShellBackPolicy.js").read_text(encoding="utf-8")

guide = qml / "KeyboardGuidePage.qml"
assert guide.exists(), "KeyboardGuidePage.qml missing"
guide_text = guide.read_text(encoding="utf-8")
assert (qml / "assets" / "keyboard-guide" / "keyboard.svg").exists(), "guide icons missing"
assert 'function takeKeyboardFocus()' in guide_text
assert 'featuresHeader.forceActiveFocus(Qt.TabFocusReason)' in guide_text
assert 'KeyNavigation.tab: readingHeader' in guide_text
assert 'KeyNavigation.tab: shortcutsHeader' in guide_text
assert 'KeyNavigation.tab: featuresHeader' in guide_text
assert 'signal keyboardGuideClicked()' in taskbar
assert 'property bool keyboardGuideActive' in taskbar
assert 'objectName: "taskbarKeyboardGuide"' in taskbar
assert taskbar.index('source: "../assets/icons/preferences.svg"') < taskbar.index('objectName: "taskbarKeyboardGuide"')
assert 'id: keyboardGuideLayer' in main
assert 'source: "KeyboardGuidePage.qml"' in main
assert 'function openKeyboardGuide()' in main
assert 'onKeyboardGuideClicked:' in main
assert 'keyboardGuideActive: keyboardGuideLayer.active' in main
assert 'keyboardGuideActive: keyboardGuideLayer.active' in main
assert 'if (on(s.keyboardGuideActive)) return "keyboardGuide"' in policy
for chord in ('Ctrl+Shift+D', 'Ctrl+Shift+E', 'Ctrl+Shift+S'):
    assert chord in main, f"missing essential shortcut {chord}"
assert 'activeFocusOnTab: true' in taskbar
assert 'Accessible.name: "Keyboard Guide"' in taskbar
print("KEYBOARD_GUIDE_INTEGRATION_OK")
