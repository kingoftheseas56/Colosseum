from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name):
    path = ROOT / "qml" / name
    assert path.exists(), f"missing {path}"
    return path.read_text(encoding="utf-8")


def require(body, *needles):
    missing = [needle for needle in needles if needle not in body]
    assert not missing, f"missing interface text: {missing}"


def test_keyboard_command_contract():
    body = source("KeyboardCommand.qml")
    require(
        body,
        "semanticId",
        "label",
        "category",
        "scope",
        "sequences",
        "enabled",
        "signal triggered",
        "function invoke",
    )


def test_keyboard_registry_contract():
    body = source("KeyboardRegistry.qml")
    require(
        body,
        "registerCommand",
        "unregisterCommand",
        "function command",
        "function snapshot",
        "function entriesFor",
        "commandTriggered",
    )


if __name__ == "__main__":
    test_keyboard_command_contract()
    test_keyboard_registry_contract()
    print("keyboard registry contract: PASS")
