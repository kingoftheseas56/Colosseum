"""Source contract: a retired Android Stream route reopens with the user's pause.

Nokia T20 (task014): returning to a player that was paused before the
background quiesce re-demanded the route and the fresh source load
autoplayed. The re-demand must record the pause state and the reopen must
re-apply it. Source assertions only; device behavior is checked on hardware.
"""
import pathlib
import re
import sys

QML = pathlib.Path(__file__).resolve().parents[1] / "qml" / "PlayerPage.qml"


def function_body(text, name):
    start = text.index(f"function {name}(")
    depth, i = 0, text.index("{", start)
    for j in range(i, len(text)):
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        if depth == 0:
            return text[i:j + 1]
    raise AssertionError(f"unterminated {name}")


def main():
    text = QML.read_text(encoding="utf-8")
    failures = []
    restore = function_body(text, "restoreRetiredStreamRoute")
    record = restore.find("root.streamRouteRestorePaused = mpv.pause")
    demand = restore.find("Stream.play(")
    if record < 0 or demand < 0 or record > demand:
        failures.append("re-demand must record mpv.pause before Stream.play")
    loaded = text[text.index("onFileLoaded: {"):]
    branch = loaded[loaded.index("if (root.streamRouteRestoreSec > 1)"):]
    branch = branch[:branch.index("} else if")]
    if not re.search(r"if \(root\.streamRouteRestorePaused\)\s*mpv\.pause = true", branch):
        failures.append("reopen branch must re-apply the recorded pause")
    if "root.streamRouteRestorePaused = false" not in branch:
        failures.append("reopen branch must consume the recorded pause")
    for f in failures:
        print("FAIL", f)
    print("android stream route restore intent:", "FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
