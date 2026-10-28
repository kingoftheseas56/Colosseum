"""Feria set 4 task 0009: headless, full-app address suggestions proof.

All network requests after the initial anonymous MANGA Plus provider launch
target a loopback ThreadingHTTPServer. No clipboard, downloads or visible GUI.
Each scenario is launched with a fresh Lanista profile tag. Full evidence is
written beneath --out; failed attempts are never omitted.
"""
import argparse
import html
import json
import os
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "feria_history_probe"))
import run_app_restart as app
import run_app_shortcuts as shortcuts

TITLES = {
    "zebra": "Zebra crossing notes",
    "zenith": "Zenith observatory",
    "apple": "Apple orchard",
    "orchard-one": "Orchard field journal",
    "orchard-two": "Orchard planning board",
    "private": "Private notebook",
    "safe": "Safe local navigation",
    "focus": "Focus typing page",
}


class Site(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        name = self.path.split("?", 1)[0].strip("/")
        if name not in TITLES:
            self.send_error(404)
            return
        title = TITLES[name]
        body = ("<!doctype html><html><head><meta charset='utf-8'><title>"
                + html.escape(title) + "</title></head><body>"
                + "<textarea id='editor' autofocus aria-label='Page keyboard target' "
                + "style='width:90vw;height:70vh'></textarea>"
                + "<script>const field=document.getElementById('editor');"
                + "field.addEventListener('input',()=>{document.title="
                + json.dumps(title + "|value=")
                + "+encodeURIComponent(field.value.toLowerCase())+'|'});"
                + "field.focus();</script></body></html>").encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def guard(label):
    return shortcuts.host_guard(label)


def go(label, site, name):
    url = site + "/" + name
    return [
        guard(label),
        app.click(label + ": open address", "feriaBrowserAddressToggle"),
        app.pause(label + ": address ready", 350),
        *app.typed(label + ": type local URL", "feriaBrowserAddress", url),
        app.key(label + ": navigate", "Return"),
        # loadSucceeded may still be true for the PREVIOUS document.
        # Guard the actual target URL before trusting its load state.
        app.wait_for(label + ": navigation reached expected URL", "feriaBrowserHost",
                     "currentUrl", url, 30000),
        app.wait_for(label + ": loaded", "feriaBrowserHost", "loadSucceeded", True, 30000),
        app.get(label + ": page address", "feriaBrowserHost", "currentUrl", "contains", "/" + name),
        app.wait_for(label + ": expected page title ready", "feriaBrowserHost",
                     "pageTitle", TITLES[name], 30000),
        app.get(label + ": page title", "feriaBrowserHost", "pageTitle", "contains", TITLES[name]),
        app.pause(label + ": title observation settles", 350),
    ]


def open_address(label):
    return [guard(label),
            app.click(label + ": open address", "feriaBrowserAddressToggle"),
            app.pause(label + ": field ready", 350)]


def query(label, text, count):
    return [
        *app.typed(label + ": text", "feriaBrowserAddress", text),
        app.wait_for(label + ": suggestions count", "feriaAddressSuggestions",
                     "count", count, 7000),
        app.get(label + ": suggestions visible", "feriaAddressSuggestions",
                "visible", "==", "true" if count else "false"),
        app.get(label + ": no highlight after edit", "feriaAddressSuggestions",
                "currentIndex", "==", -1),
    ]


def row(label, number, title, url_suffix):
    return [app.get(label + ": title", "feriaAddressSuggestionTitle_" + str(number),
                    "text", "==", title),
            app.get(label + ": address", "feriaAddressSuggestionUrl_" + str(number),
                    "text", "contains", url_suffix)]


def history(site):
    steps = app.boot(True)
    for name in ("zebra", "zenith", "apple"):
        steps += go("history " + name, site, name)
    steps += open_address("history search")
    steps += [app.get("untouched page address has no suggestions", "feriaAddressSuggestions",
                      "visible", "==", "false"),
              app.get("untouched suggestions count", "feriaAddressSuggestions", "count", "==", 0)]
    steps += query("ze", "ze", 2)
    steps += row("newest history result", 0, TITLES["zenith"], "/zenith")
    steps += row("older history result", 1, TITLES["zebra"], "/zebra")
    steps += query("zeb", "zeb", 1)
    steps += row("one narrowed result", 0, TITLES["zebra"], "/zebra")
    steps += query("qqq", "qqq", 0)
    steps += query("empty", "", 0)
    steps += app.key("Escape closes address", "Escape"),
    steps += app.QUIT
    return steps


def keyboard(site):
    steps = (app.boot(True) + go("keyboard zebra", site, "zebra")
             + go("keyboard zenith", site, "zenith")
             + go("keyboard source apple", site, "apple"))
    steps += open_address("keyboard query zen")
    steps += query("keyboard zen", "zen", 1)
    steps += [app.key("Down highlights first row", "Down"),
              app.get("first row highlighted", "feriaAddressSuggestions", "currentIndex", "==", 0),
              app.key("Enter opens highlighted row", "Return"),
              app.wait_for("chosen URL navigated", "feriaBrowserHost", "currentUrl",
                           site + "/zenith", 20000),
              app.wait_for("chosen page title ready", "feriaBrowserHost", "pageTitle",
                           TITLES["zenith"], 30000),
              app.get("chosen page title", "feriaBrowserHost", "pageTitle",
                      "contains", TITLES["zenith"]),
              app.get("chosen row popup dismissed", "feriaAddressSuggestions", "visible", "==", "false")]
    steps += open_address("keyboard highlight reset")
    steps += query("keyboard ze", "ze", 2)
    steps += [app.key("Down to first", "Down"),
              app.get("first highlighted", "feriaAddressSuggestions", "currentIndex", "==", 0),
              app.key("Down to second", "Down"),
              app.get("second highlighted", "feriaAddressSuggestions", "currentIndex", "==", 1),
              app.key("Up to first", "Up"),
              app.get("first after Up", "feriaAddressSuggestions", "currentIndex", "==", 0),
              app.key("Up back to field", "Up"),
              app.get("no row highlighted", "feriaAddressSuggestions", "currentIndex", "==", -1),
              app.get("field retains typing focus", "feriaBrowserAddress", "activeFocus", "==", "true")]
    # Bare 'ze' intentionally is NOT submitted: native omniboxUrl('ze') opens
    # DuckDuckGo/Google/Bing, forbidden by this task's public-site restriction.
    # Verify normal unhighlighted Enter with a safe loopback URL instead.
    steps += query("safe unhighlighted navigation", site + "/safe", 0)
    steps += [app.key("unhighlighted Enter navigates as typed", "Return"),
              app.wait_for("typed safe URL navigated", "feriaBrowserHost",
                           "currentUrl", site + "/safe", 20000),
              app.get("typed safe URL page", "feriaBrowserHost", "pageTitle",
                      "contains", TITLES["safe"])]
    steps += app.QUIT
    return steps


def bookmarks(site):
    steps = app.boot(True) + go("bookmark apple", site, "apple")
    steps += [app.key("open bookmarks", "Ctrl+D"),
              app.wait_for("bookmarks menu visible", "feriaBrowserBookmarksList", "visible", True, 10000),
              app.click("save apple", "feriaBrowserBookmarkCurrent"),
              app.wait_for("one saved bookmark", "feriaBrowserBookmarksList", "count", 1, 10000),
              app.click("close bookmarks", "feriaBrowserMenuDismiss"),
              app.pause("menu dismisses", 350)]
    steps += go("other orchard one", site, "orchard-one")
    steps += go("other orchard two", site, "orchard-two")
    steps += open_address("orchard search")
    steps += query("orchard", "orchard", 3)
    steps += row("bookmark precedes newer history", 0, TITLES["apple"], "/apple")
    steps += row("newest unbookmarked history", 1, TITLES["orchard-two"], "/orchard-two")
    steps += row("older unbookmarked history", 2, TITLES["orchard-one"], "/orchard-one")
    steps += [app.key("close suggestion popup", "Escape")] + app.QUIT
    return steps


def private(site):
    steps = app.boot(True) + go("private seed zebra", site, "zebra") + go("private seed zenith", site, "zenith")
    steps += [app.key("open private tab", "Ctrl+Shift+N"),
              app.pause("private tab initializes", 1400)]
    steps += go("private-only visit", site, "private")
    steps += open_address("private history suggestions")
    steps += query("private ze", "ze", 2)
    steps += row("private first", 0, TITLES["zenith"], "/zenith")
    steps += row("private second", 1, TITLES["zebra"], "/zebra")
    steps += [app.click("mouse chooses saved row", "feriaAddressSuggestion_0"),
              app.wait_for("mouse navigated privately", "feriaBrowserHost",
                           "currentUrl", site + "/zenith", 20000),
              app.wait_for("mouse-chosen page title ready", "feriaBrowserHost",
                           "pageTitle", TITLES["zenith"], 30000),
              app.get("mouse-chosen page title", "feriaBrowserHost", "pageTitle",
                      "contains", TITLES["zenith"]),
              app.key("open private History", "Ctrl+H"),
              app.wait_for("History list visible", "feriaBrowserHistoryList",
                           "visible", True, 10000),
              app.get("private identity", "feriaBrowserPrivacy", "text", "contains", "Private"),
              *app.typed("search private-only history", "feriaBrowserHistorySearch", "/private"),
              app.wait_for("private visit not recorded", "feriaBrowserHistoryList", "count", 0, 7000),
              app.click("dismiss History", "feriaBrowserMenuDismiss"),
              app.pause("History closes", 300)]
    steps += app.QUIT
    return steps


def escape(site):
    steps = app.boot(True) + go("escape zebra", site, "zebra") + go("escape zenith", site, "zenith")
    steps += go("escape focus page", site, "focus")
    steps += shortcuts.page_focus("escape focus control")
    steps += [app.key("page receives A first", "A"),
              app.pause("page title updates A", 1300),
              app.get("textarea held keyboard before popup", "feriaBrowserHost",
                      "pageTitle", "contains", "|value=a|"),
              app.key("Ctrl+L opens address from web keyboard", "Ctrl+L"),
              app.wait_for("address field focused", "feriaBrowserAddress",
                           "activeFocus", True, 8000)]
    steps += query("Escape ze", "ze", 2)
    steps += [app.key("Escape closes suggestions and field", "Escape"),
              app.pause("focus handoff", 600),
              app.get("suggestions leave with popup", "feriaAddressSuggestions",
                      "visible", "==", "false"),
              app.get("model clears when popup closes", "feriaAddressSuggestions",
                      "count", "==", 0),
              app.key("plain B goes straight to page without click", "B"),
              app.pause("page reports B", 1400),
              app.get("textarea automatically regains keyboard", "feriaBrowserHost",
                      "pageTitle", "contains", "|value=ab|")]
    steps += app.QUIT
    return steps


SCENARIOS = {"history": history, "keyboard": keyboard, "bookmarks": bookmarks,
             "private": private, "escape": escape}


def run(args, name, serial, site):
    out = args.out / ("%s-%02d" % (name, serial))
    out.mkdir(parents=True, exist_ok=True)
    scenario = out / "suggestions.json"
    scenario.write_text(json.dumps({"name": "suggestions-" + name, "steps": SCENARIOS[name](site)},
                                   indent=1), encoding="utf-8")
    env = dict(os.environ)
    env["PATH"] = str(args.qt_bin) + os.pathsep + env.get("PATH", "")
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    tag = "feria-set4-0009-%s-%02d-%s" % (name, serial, time.strftime("%Y%m%d-%H%M%S"))
    cmd = [str(args.lanista), "session", "run", str(scenario),
           "--exe", str(args.exe), "--qml", "qml/Main.qml", "--drive", "--tag", tag]
    # Stream Lanista output to disk as it arrives. A failed app may leave a
    # descendant holding an inherited stdout handle, so communicate() can
    # wait even after Lanista exits. A daemon pump and bounded join preserve
    # the first failing step without waiting on that unrelated descendant.
    log = out / "suggestions.out"
    with log.open("w", encoding="utf-8", errors="replace") as capture:
        proc = subprocess.Popen(cmd, cwd=app.ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                encoding="utf-8", errors="replace", bufsize=1)
        def pump():
            for line in proc.stdout:
                capture.write(line)
                capture.flush()
        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        try:
            code = proc.wait(timeout=180)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)
            code = -1
            capture.write("\nSCENARIO TIMEOUT after 180 seconds\n")
            capture.flush()
        reader.join(timeout=5)
    log_text = log.read_text(encoding="utf-8", errors="replace")
    summary = re.search(r"(\d+) steps, (\d+) failed", log_text)
    match = re.search(r"manifest: (.*?)\)", log_text)
    manifest = {}
    if match:
        path = app.ROOT / match.group(1)
        if path.exists():
            manifest = json.loads(path.read_text(encoding="utf-8"))
            (out / "manifest.json").write_text(json.dumps(manifest, indent=1),
                                               encoding="utf-8")
    clean = (manifest.get("exitCode") == 0 and manifest.get("crashed") is False
             and manifest.get("killReason") == "graceful")
    ok = code == 0 and clean and bool(summary) and summary.group(2) == "0"
    print(("PASS" if ok else "FAIL"), name, "attempt", serial,
          summary.group(0) if summary else "NO SUMMARY",
          "lanista", code, "app", manifest.get("exitCode"),
          "crashed", manifest.get("crashed"), "exit", manifest.get("killReason"),
          "log", str(log), flush=True)
    if not ok:
        for n, line in enumerate(log_text.splitlines(), start=1):
            if line.startswith(("FAIL", "INFRA")) or "Error" in line or "error:" in line:
                print("  line", n, line[:250], flush=True)
    return ok


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--lanista", required=True, type=Path)
    parser.add_argument("--qt-bin", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--scenario", choices=[*SCENARIOS, "all"], default="all")
    parser.add_argument("--runs", type=int, default=2)
    parser.add_argument("--max-attempts", type=int, default=4)
    args = parser.parse_args()
    args.exe, args.lanista = args.exe.resolve(), args.lanista.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Site)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    site = "http://127.0.0.1:%d" % server.server_address[1]
    print("LOOPBACK", site, flush=True)
    results = {}
    try:
        for name in (SCENARIOS if args.scenario == "all" else [args.scenario]):
            results[name] = []
            for attempt in range(1, args.max_attempts + 1):
                if sum(results[name]) >= args.runs:
                    break
                results[name].append(run(args, name, attempt, site))
    finally:
        server.shutdown()
        server.server_close()
    (args.out / "attempts.json").write_text(json.dumps(results, indent=1),
                                              encoding="utf-8")
    passed = all(sum(v) >= args.runs for v in results.values())
    print("SUGGESTIONS PROOF", "PASS" if passed else "FAIL", results, flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
