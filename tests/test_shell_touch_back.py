"""Run the production shell mouse-back handler against real Qt touch/mouse events."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qmltestrunner")
    args = parser.parse_args()
    source = (ROOT / "qml/Main.qml").read_text(encoding="utf-8")
    start = source.index("    Item {", source.index("// The mouse's Back button"))
    end = source.index("    // Home is the bottom", start)
    handler = source[start:end]
    test = '''import QtQuick
import QtTest
Item {
    id: surface
    width: 200; height: 200
    property int touchActions: 0
    QtObject {
        id: escapeCommand
        property int calls: 0
        function invoke(reason) { calls++ }
    }
    TapHandler { onTapped: surface.touchActions++ }
    HANDLER
    TestCase {
        name: "ShellTouchBack"
        when: windowShown
        function init() { escapeCommand.calls = 0; surface.touchActions = 0 }
        function test_touch_does_not_navigate_back() {
            touchEvent(surface).press(0, surface, 100, 100).commit()
            touchEvent(surface).release(0, surface, 100, 100).commit()
            wait(50)
            compare(escapeCommand.calls, 0)
            compare(surface.touchActions, 1)
        }
        function test_mouse_back_still_works() {
            mouseClick(surface, 100, 100, Qt.BackButton)
            compare(escapeCommand.calls, 1)
        }
    }
}
'''.replace("HANDLER", handler)
    with tempfile.TemporaryDirectory(prefix="colosseum-touch-back-") as directory:
        (Path(directory) / "tst_touch.qml").write_text(test, encoding="utf-8")
        env = dict(os.environ, QT_QUICK_BACKEND="software")
        report = Path(directory) / "results.txt"
        result = subprocess.run([args.qmltestrunner, "-platform", "offscreen", "-input", directory,
                                 "-o", str(report) + ",txt"], env=env, timeout=45)
        print(report.read_text(encoding="utf-8") if report.exists() else "Qt test runner produced no report")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
