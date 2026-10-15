from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_home_shell_bridges_topbar_and_content_directionally():
    main = (ROOT / "qml" / "Main.qml").read_text(encoding="utf-8")
    required = [
        "id: homePageSpatialNav",
        "id: homeTopSpatialNav",
        "onBoundaryArrowRequested:",
        "homePageSpatialNav.moveFrom(fromItem, key)",
        "homeTopSpatialNav.moveFrom(fromItem, key)",
        "homePageSpatialNav.handle(event)",
        'objectName: slide.SwipeView.isCurrentItem ? "homeHeroExplore" : ""',
        'objectName: "homeUniverseHall"',
        'accessibleName: "Explore the universe"',
        'accessibleName: "Hall of Worlds"',
        "function focusHomePrimary()",
        'homePageSpatialNav.focusNamed("homeHeroExplore"',
        "keyboardIgnition.ignite(event.key, event.modifiers)",
    ]
    missing = [needle for needle in required if needle not in main]
    assert not missing, "missing Home spatial bridge: " + ", ".join(missing)


if __name__ == "__main__":
    test_home_shell_bridges_topbar_and_content_directionally()
    print("PLAYSTATION_SPATIAL_SHELL_CONTRACT_OK")
