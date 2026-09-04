from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_runtime_surfaces_have_stable_automation_identities():
    galaxy = text("qml/GalaxyUniversePage.qml")
    system = text("qml/StarWarsGalaxySystem.qml")
    settings = text("qml/SettingsPage.qml")
    main = text("qml/Main.qml")
    required = [
        (galaxy, 'objectName: "galaxyUniversePage"'),
        (system, 'objectName: "starWarsGalaxySystem"'),
        (system, 'objectName: "starWarsGalaxySkywalker"'),
        (system, 'objectName: "starWarsGalaxyGate_" + gate.modelData.id'),
        (settings, 'objectName: "settingsPage"'),
        (settings, 'objectName: "settingsPageScroll"'),
        (main, 'objectName: "universeLayer"'),
    ]
    missing = [needle for haystack, needle in required if needle not in haystack]
    assert not missing, "missing runtime automation identities: " + ", ".join(missing)


if __name__ == "__main__":
    test_runtime_surfaces_have_stable_automation_identities()
    print("ARC41_RUNTIME_IDENTITY_CONTRACT_OK")
