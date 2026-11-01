"""Exercise the retained Vault composition's unavailable mouse and keyboard routes."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    root = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "qml"
    qml = '''import QtQuick
import QtTest
import "QML_URI" as UI
Item {
    width:1280; height:720
    Item { id: wall }
    UI.VaultDoor { id: door; width:46; height:46; available:false }
    UI.VaultHomeWidget { id: widget; x:60; width:1100; height:520; backdrop:wall; available:false }
    UI.Taskbar { id: dock; z:20 }
    SignalSpy { id: collectionsClicks; target:dock; signalName:"collectionsClicked" }
    SignalSpy { id: doorClicks; target:door; signalName:"clicked" }
    SignalSpy { id: widgetClicks; target:widget; signalName:"clicked" }
    TestCase {
        name:"UnavailableVaultComposition"
        when:windowShown
        function test_master_dock_retains_collections_api() {
            compare(dock.closedWidth,190)
            const action = findChild(dock,"taskbarCollectionsAction")
            verify(action !== null)
            compare(action.enabled,true)
            mouseClick(action,10,10)
            compare(collectionsClicks.count,1)
        }
        function test_unavailable_routes_do_not_emit_navigation() {
            mouseClick(door,23,23)
            mouseClick(widget,500,260)
            door.forceActiveFocus(); keyClick(Qt.Key_Return)
            widget.forceActiveFocus(); keyClick(Qt.Key_Return)
            compare(doorClicks.count,0); compare(widgetClicks.count,0)
            compare(findChild(door,"vaultDoorKeyboardAction").enabled,false)
            compare(findChild(widget,"vaultHomeWidgetKeyboardAction").enabled,false)
        }
        function test_desktop_available_routes_still_work() {
            door.available = true; widget.available = true
            mouseClick(door,23,23); mouseClick(widget,500,260)
            compare(doorClicks.count,1); compare(widgetClicks.count,1)
            door.available = false; widget.available = false
            doorClicks.clear(); widgetClicks.clear()
        }
    }
}
'''.replace("QML_URI", root.resolve().as_uri())
    with tempfile.TemporaryDirectory(prefix="vault-availability-") as directory:
        path = Path(directory)
        (path / "tst_vault.qml").write_text(qml, encoding="utf-8")
        report = path / "results.txt"
        result = subprocess.run([sys.argv[1], "-platform", "offscreen", "-input", directory,
            "-o", str(report) + ",txt"], env=dict(os.environ, QT_QUICK_BACKEND="software"), timeout=30)
        print(report.read_text(encoding="utf-8") if report.exists() else "No Qt test report")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
