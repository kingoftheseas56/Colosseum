from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def require(rel, *needles):
    body = text(rel)
    missing = [needle for needle in needles if needle not in body]
    assert not missing, f"{rel}: missing {missing}"


# Root TV mode is an integration seam: W02 may expose native Android TV
# detection, while desktop and ordinary Android remain false by default.
require("qml/Main.qml", "televisionMode", "PlatformRuntime", "androidTelevision")

# Remote center/select must reuse the existing semantic activation layer.
require("qml/KeyboardAction.qml", "Qt.Key_Select", "televisionMode", "focusFrameWidth")
require("qml/KeyboardCollectionController.qml", "Qt.Key_Select")
require("qml/WorldTabBar.qml", "Qt.Key_Select")
require("qml/TheatreTabBar.qml", "Qt.Key_Select")
require("qml/PosterRail.qml", "Qt.Key_Select")
require("qml/CataloguePosterGrid.qml", "Qt.Key_Select")
# TV-only composite focus keeps left/right local while unhandled up/down can
# hand off through the world's existing tab order.
require("qml/FeaturedCarousel.qml", "televisionMode", "activeFocusOnTab", "Qt.Key_Select")
require("qml/ContinueRow.qml", "televisionMode", "currentIndex", "Qt.Key_Left", "Qt.Key_Right", "Qt.Key_Select")
require("qml/ContinueTile.qml", "focusManagedByCollection", "keyboardFocused")
require("qml/WorldPage.qml", "televisionMode", "Keys.priority: Keys.AfterItem",
        "moveVerticalFocus", "Qt.Key_Up", "Qt.Key_Down")

# Shell chrome remains the same product surface, but a D-pad can traverse it.
require("qml/TopBar.qml", "televisionMode", "moveHorizontalFocus", "Keys.priority: Keys.AfterItem")
require("qml/Taskbar.qml", "televisionMode", "moveHorizontalFocus", "Keys.priority: Keys.AfterItem")
taskbar = text("qml/Taskbar.qml")
for select_action in ("bar.open = !bar.open", "bar.downloadsClicked()", "bar.extensionsClicked()",
                      "bar.settingsClicked()", "bar.keyboardGuideClicked()", "fanRow.activateSession()"):
    assert f"event.key === Qt.Key_Select) {{ {select_action}" in taskbar, (
        f"Taskbar direct-focus action lacks Android TV Select parity: {select_action}")

print("android tv navigation contract: PASS")
