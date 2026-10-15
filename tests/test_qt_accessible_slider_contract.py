from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QML = ROOT / "qml"


def test_accessible_slider_value_is_not_an_attached_property():
    offenders = []
    for path in QML.rglob("*.qml"):
        for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if "Accessible.value:" in line:
                offenders.append(f"{path.relative_to(ROOT)}:{line_no}")
    assert not offenders, (
        "Qt 6.11 Accessible has no attached `value` property; slider value semantics "
        "must be exposed as value/minimumValue/maximumValue/stepSize on the item: "
        + ", ".join(offenders)
    )


if __name__ == "__main__":
    test_accessible_slider_value_is_not_an_attached_property()
    print("QT_ACCESSIBLE_SLIDER_CONTRACT_OK")
