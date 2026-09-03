from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "qml" / "Main.qml").read_text(encoding="utf-8")
GUIDE = (ROOT / "qml" / "KeyboardGuidePage.qml").read_text(encoding="utf-8")


GLOBAL_COMMANDS = {
    "global.openMedia": "Ctrl+O",
    "global.openVault": "Ctrl+Shift+V",
    "global.openDownloads": "Ctrl+Shift+D",
    "global.openExtensions": "Ctrl+Shift+E",
    "global.openSettings": "Ctrl+Shift+S",
    "global.fullscreen": "F11",
    "global.quit": "Ctrl+Q",
    "global.escape": "Escape",
}


def test_main_owns_and_registers_global_commands():
    assert "KeyboardRegistry" in MAIN
    assert "id: keyboardRegistry" in MAIN
    assert "registerCommand" in MAIN
    for semantic_id, sequence in GLOBAL_COMMANDS.items():
        assert semantic_id in MAIN, f"missing semantic command {semantic_id}"
        assert sequence in MAIN, f"missing sequence {sequence}"


def test_guide_consumes_registry_instead_of_owning_shortcut_rows():
    assert "property QtObject keyboardRegistry" in GUIDE
    assert "entriesFor" in GUIDE
    assert "keyboardRegistry.revision" in GUIDE
    assert "readonly property var shortcutRows: [" not in GUIDE


if __name__ == "__main__":
    test_main_owns_and_registers_global_commands()
    test_guide_consumes_registry_instead_of_owning_shortcut_rows()
    print("keyboard registry guide parity: PASS")
