from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def text(name):
    path = ROOT / "qml" / name
    assert path.exists(), f"missing {path}"
    return path.read_text(encoding="utf-8")


def require(body, *needles):
    missing = [needle for needle in needles if needle not in body]
    assert not missing, f"missing interface text: {missing}"


def test_keyboard_region_contract():
    require(
        text("KeyboardRegion.qml"),
        "FocusScope",
        "regionId",
        "entryItem",
        "lastFocusItem",
        "tabNext",
        "tabPrevious",
        "returnFocusItem",
        "trapTab",
        "function focusEntry",
        "function rememberFocus",
        "function restoreFocus",
        "escapeRequested",
    )


def test_keyboard_action_command_contract():
    require(text("KeyboardAction.qml"), "property QtObject command", "command.invoke")


if __name__ == "__main__":
    test_keyboard_region_contract()
    test_keyboard_action_command_contract()
    print("keyboard region contract: PASS")
