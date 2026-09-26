from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "qml" / "Main.qml").read_text(encoding="utf-8")


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


if __name__ == "__main__":
    test_main_owns_and_registers_global_commands()
    print("keyboard registry: PASS")
