"""Run Main's actual initial-surface readiness Timer against controlled visible geometry."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / "qml/Main.qml").read_text(encoding="utf-8")
    start = source.index("    property bool androidHomeReadyReported" if "    property bool androidHomeReadyReported" in source else "    property bool androidWorldReadyReported")
    end = source.index("    // ---- world pages:", start)
    readiness = source[start:end]
    if "id: androidInitialSurfaceReadiness" not in readiness:
        readiness = readiness.replace("Timer {", "Timer { id: androidInitialSurfaceReadiness", 1)
    qml = r'''import QtQuick
import QtTest
Item {
    id: win
    width:1280; height:720
    property bool androidHost:false
    property bool desktopPresentation:true
    QtObject { id: boot; property bool visible:false }
    QtObject { id: accountHost; property bool visible:false }
    QtObject { id: worldStack; property string current:"" }
    Item { id: topbar; width:1172; height:56 }
    Flickable { id: page; width:1280; height:620 }
    Loader { id: worldLoader; property string mode:"Theatre"; active:false; sourceComponent:Component { Item {} } }
    QtObject { id: worldRepeater; property int count:1; function itemAt(index) { return worldLoader } }
    READINESS
    TestCase {
        name:"AndroidInitialSurfaceReadiness"
        when:windowShown
        function init() {
            win.androidHost = false
            win.desktopPresentation = true
            win.visible = true; boot.visible = false; accountHost.visible = false
            topbar.visible = true; topbar.width = 1172; topbar.height = 56
            page.visible = true; page.width = 1280; page.height = 620
            worldStack.current = ""; worldLoader.active = false; worldLoader.visible = true
            win.androidWorldReadyReported = false
            if (win.androidHomeReadyReported !== undefined) win.androidHomeReadyReported = false
        }
        function test_home_tick_reports_home_only_and_stops_timer() {
            win.androidHost = true
            tryCompare(win, "androidHomeReadyReported", true, 500)
            compare(win.androidWorldReadyReported, false)
            compare(androidInitialSurfaceReadiness.running, false)
            worldStack.current = "Theatre"; worldLoader.active = true
            androidInitialSurfaceReadiness.triggered()
            compare(win.androidWorldReadyReported, false)
        }
        function test_hidden_and_zero_geometry_home_never_reports() {
            const cases = [[win,"visible",false],[boot,"visible",true],[accountHost,"visible",true],
                [page,"visible",false],[topbar,"visible",false],[page,"width",0],[page,"height",0],
                [topbar,"width",0],[topbar,"height",0]]
            for (const row of cases) {
                const previous = row[0][row[1]]
                row[0][row[1]] = row[2]
                androidInitialSurfaceReadiness.triggered()
                compare(win.androidHomeReadyReported, false)
                compare(win.androidWorldReadyReported, false)
                row[0][row[1]] = previous
            }
        }
        function test_legacy_phone_home_is_not_ready() {
            win.desktopPresentation = false
            win.androidHost = true
            androidInitialSurfaceReadiness.triggered()
            compare(win.androidHomeReadyReported, false)
            compare(win.androidWorldReadyReported, false)
            compare(androidInitialSurfaceReadiness.running, true)
        }
        function test_world_still_requires_loaded_visible_item() {
            worldStack.current = "Theatre"
            androidInitialSurfaceReadiness.triggered()
            compare(win.androidWorldReadyReported, false)
            worldLoader.active = true
            compare(worldLoader.status, Loader.Ready)
            worldLoader.visible = false
            androidInitialSurfaceReadiness.triggered()
            compare(win.androidWorldReadyReported, false)
            worldLoader.visible = true
            win.androidHost = true
            tryCompare(win, "androidWorldReadyReported", true, 500)
            compare(win.androidHomeReadyReported, false)
            compare(androidInitialSurfaceReadiness.running, false)
        }
        function cleanup() { win.androidHost = false; win.visible = true }
    }
}
'''.replace("READINESS", readiness)
    with tempfile.TemporaryDirectory(prefix="colosseum-readiness-") as directory:
        path = Path(directory)
        (path / "tst_readiness.qml").write_text(qml, encoding="utf-8")
        report = path / "results.txt"
        result = subprocess.run([sys.argv[1], "-platform", "offscreen", "-input", directory,
            "-o", str(report) + ",txt"], env=dict(os.environ, QT_QUICK_BACKEND="software"), timeout=30)
        print(report.read_text(encoding="utf-8") if report.exists() else "No Qt test report")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
