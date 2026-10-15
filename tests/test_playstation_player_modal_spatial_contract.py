from pathlib import Path


def test_player_modal_button_rows_are_spatial_focus_universes():
    src = Path("qml/PlayerPage.qml").read_text(encoding="utf-8")

    required = [
        "id: resumeChoicePanel",
        "id: resumeChoiceSpatial",
        "root: resumeChoicePanel",
        "resumeChoiceSpatial.handle(event)",
        "id: closeConfirmPanel",
        "id: closeConfirmSpatial",
        "root: closeConfirmPanel",
        "closeConfirmSpatial.handle(event)",
    ]
    missing = [needle for needle in required if needle not in src]
    assert not missing, "missing Player modal spatial wiring: " + ", ".join(missing)


if __name__ == "__main__":
    test_player_modal_button_rows_are_spatial_focus_universes()
    print("PLAYSTATION_PLAYER_MODAL_SPATIAL_CONTRACT_OK")
