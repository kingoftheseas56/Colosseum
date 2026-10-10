"""Feria set 4: real-app keyboard shortcuts while WebEngine textarea owns focus.

    python tests/feria_daily_probe/run_app_shortcuts.py \
       --exe C:/b/feria-foundation/native/build-msvc/colosseum.exe \
       --lanista C:/b/feria-foundation/native/build-msvc/lanista.exe \
       --qt-bin C:/Qt/6.11.1/msvc2022_64/bin \
       --out C:/b/ff-results/set4/task-0006/app-shortcuts-01

All browsing after the initial anonymous MANGA Plus launch uses a local HTTP
server. The page reports keyboard input, keydown counts and navigation load
counts through document.title, which is read from feriaBrowserHost.pageTitle.
No clipboard operations, download requests, or PDF saves are performed.
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


class KeyboardSite(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        name = self.path.split("?", 1)[0].strip("/") or "a"
        safe = html.escape(name, quote=True)
        body = ("""<!doctype html><html><head><meta charset="utf-8"></head>
<body style="margin:0;background:#1a2b37">
<textarea id="editor" autofocus aria-label="Page keyboard target"
 style="box-sizing:border-box;width:100vw;height:100vh;margin:0;padding:24px;
 font:22px monospace;resize:none"></textarea>
<script>
(function() {
    const name = %r;
    const textKey = 'kbd-value:' + name;
    const loadKey = 'kbd-loads:' + name;
    const loads = 1 + Number(sessionStorage.getItem(loadKey) || 0);
    sessionStorage.setItem(loadKey, String(loads));
    const field = document.getElementById('editor');
    field.value = sessionStorage.getItem(textKey) || '';
    let keydowns = 0;
    function report() {
        document.title = 'kbd=' + name + '|loads=' + loads
            + '|value=' + encodeURIComponent(field.value.toLowerCase())
            + '|keydown=' + keydowns + '|';
    }
    document.addEventListener('keydown', function() { ++keydowns; report(); });
    field.addEventListener('input', function() {
        sessionStorage.setItem(textKey, field.value);
        report();
    });
    field.focus();
    report();
})();
</script></body></html>""" % name).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)



def title(label, value):
    return app.get(label, "feriaBrowserHost", "pageTitle", "contains", value)


def observed(label, obj, prop):
    """A non-asserting read for a disputed focus transfer."""
    return {"cmd": "qml-get", "label": label,
            "payload": {"object": obj, "props": [prop]}}


def title_ready(label, name, loads=1, value=""):
    # ui-wait-for compares entire values, not substrings. The newly loaded
    # document has received no keys, so its first report is deterministic.
    marker = "kbd=%s|loads=%d|value=%s|keydown=0|" % (name, loads, value)
    return app.wait_for(label + ": page title marker ready",
                        "feriaBrowserHost", "pageTitle", marker, 15000)


def host_guard(label):
    return app.get(label + ": GUARD viewState is host",
                   "feriaAppPage", "viewState", "==", "host")


def page_focus(label):
    # The guard MUST abort the scenario on failure. Lanista runs without
    # --keep-going: a home-screen tile must never be clicked as a web page.
    return [host_guard(label),
            app.wait_for(label + ": hosted page ready", "feriaBrowserHost",
                         "loadSucceeded", True, 15000),
            app.click(label + ": first focus click", "feriaQtWebEngine"),
            app.pause(label + ": first Chromium focus hand-off", 500),
            app.click(label + ": reaffirm focus in textarea", "feriaQtWebEngine"),
            app.pause(label + ": Chromium focus hand-off", 850)]


def unchanged(label, name, value):
    return [app.pause(label + ": title update", 450),
            title(label + ": textarea unchanged",
                  "kbd=%s|" % name if value is None
                  else "|value=%s|" % value)]


def prove_focus(steps, label, name, values, letter):
    """Show the web textarea actually receives an ordinary key immediately
    before each shortcut; keep the exact expected lowercased input value."""
    current = values.get(name, "") + letter.lower()
    steps.extend(page_focus(label))
    steps += [app.key(label + ": CONTROL plain " + letter + " reaches page", letter),
              app.pause(label + ": wait for input title", 1600),
              title(label + ": CONTROL keyboard owned by " + name,
                    "kbd=%s|" % name),
              title(label + ": CONTROL textarea value", "|value=%s|" % current)]
    values[name] = current
    return current


def address(label, url, name, loads=1, value=""):
    # Do not attempt to address a browser that has returned to Feria home.
    return [host_guard(label), app.click(label + ": open address", "feriaBrowserAddressToggle"),
            app.pause(label + ": address ready", 350),
            *app.typed(label + ": enter local URL", "feriaBrowserAddress", url),
            app.key(label + ": navigate", "Return"),
            app.wait_for(label + ": loading completed",
                         "feriaBrowserHost", "loadSucceeded", True, 30000),
            app.get(label + ": local address", "feriaBrowserHost",
                    "currentUrl", "contains", url),
            title_ready(label, name, loads, value)]


def browser_menu(label, section, count=None):
    steps = [app.key(label + ": open browser menu", "Ctrl+H"),
             app.wait_for(label + ": History opens first",
                          "feriaBrowserHistoryList", "visible", True, 10000)]
    if section != "History":
        steps += [app.click(label + ": choose " + section + " section",
                            "feriaBrowser" + section + "Section"),
                  app.wait_for(label + ": " + section + " section visible",
                               "feriaBrowser" + section + "List", "visible", True, 10000)]
    if count is not None:
        steps.append(app.wait_for(label + ": tabs count " + str(count),
                                  "feriaBrowserTabsList", "count", count, 10000))
    return steps


def close_menu(label):
    return [app.click(label + ": dismiss browser menu", "feriaBrowserMenuDismiss"),
            app.pause(label + ": modal closes", 300)]


def reopen_provider(label):
    # Called ONLY after a successful 15-second home-state wait. The navigation
    # is the same documented 800x800 keyboard route as app.boot().
    return [app.get(label + ": GUARDED home before tile", "feriaAppPage",
                    "viewState", "==", "home"),
            app.click(label + ": Feria mode", "modePill_Feria"),
            app.key(label + ": tile 2", "Right"),
            app.key(label + ": tile 3", "Right"),
            app.key(label + ": tile 6", "Down"),
            app.key(label + ": tile 9", "Down"),
            app.key(label + ": tile 12", "Down"),
            app.key(label + ": tile 11 MANGA Plus", "Left"),
            app.pause(label + ": focus settles", 350),
            app.get(label + ": GUARDED home before provider click",
                    "feriaAppPage", "viewState", "==", "home"),
            app.click(label + ": reopen MANGA Plus", "feriaApp_mangaplus"),
            app.wait_for(label + ": returned to browser host",
                         "feriaAppPage", "viewState", "host", 15000),
            app.wait_for(label + ": hosted browser loaded",
                         "feriaBrowserHost", "loadSucceeded", True, 60000)]


def navigation(site):
    s, values = app.boot(True), {}
    s += address("navigation A", site + "/a", "a")
    a = prove_focus(s, "Ctrl+L", "a", values, "A")
    s += [app.key("Ctrl+L: open address", "Ctrl+L"),
          app.wait_for("Ctrl+L: editor focused", "feriaBrowserAddress",
                       "activeFocus", True, 10000)]
    s += unchanged("Ctrl+L", "a", a)
    s += [app.key("Ctrl+L: Escape close address", "Escape"),
          app.pause("Ctrl+L: let Escape dismiss popup", 550),
          observed("Ctrl+L: editor activeFocus after Escape",
                   "feriaBrowserAddress", "activeFocus"),
          app.key("Ctrl+L: test page keyboard WITHOUT click (B)", "B"),
          app.pause("Ctrl+L: allow unsummoned page input", 650),
          observed("Ctrl+L: Escape page focus finding (value=ab means automatic)",
                   "feriaBrowserHost", "pageTitle")]
    # Escape focus is an observation, not a blocking assertion; the later keys
    # start from a new loopback document, independently of this result.
    s += address("navigation reload", site + "/reload", "reload")
    r = prove_focus(s, "Ctrl+R", "reload", values, "C")
    s += [app.key("Ctrl+R: reload", "Ctrl+R"),
          title_ready("Ctrl+R: count becomes 2", "reload", 2, r)]
    s += unchanged("Ctrl+R", "reload", r)
    r = prove_focus(s, "F5", "reload", values, "D")
    s += [app.key("F5: reload", "F5"),
          title_ready("F5: count becomes 3", "reload", 3, r)]
    s += unchanged("F5", "reload", r)
    s += address("navigation B", site + "/b", "b")
    b = prove_focus(s, "Alt+Left", "b", values, "E")
    s += [app.key("Alt+Left: go back", "Alt+Left"),
          app.wait_for("Alt+Left: previous URL", "feriaBrowserHost",
                       "currentUrl", site + "/reload", 15000),
          app.pause("Alt+Left: title catch-up", 1800),
          title("Alt+Left: previous title marker", "kbd=reload|")]
    r = prove_focus(s, "Alt+Right", "reload", values, "F")
    s += [app.key("Alt+Right: go forward", "Alt+Right"),
          app.wait_for("Alt+Right: B URL", "feriaBrowserHost",
                       "currentUrl", site + "/b", 15000),
          app.pause("Alt+Right: title catch-up", 1800),
          title("Alt+Right: B title marker", "kbd=b|")]
    s += unchanged("Alt+Left and Alt+Right: B kept input", "b", b)
    s += app.QUIT
    return s


def tabs(site):
    s, values = app.boot(True), {}
    s += address("tab B", site + "/b", "b")
    b = prove_focus(s, "Ctrl+T", "b", values, "B")
    s += [app.key("Ctrl+T: create tab", "Ctrl+T"),
          app.pause("Ctrl+T: new tab initialized", 1000)]
    s += browser_menu("Ctrl+T", "Tabs", 2)
    s += close_menu("Ctrl+T")
    s += address("tab C", site + "/c", "c")
    c = prove_focus(s, "Ctrl+Tab", "c", values, "C")
    s += [app.key("Ctrl+Tab: cycle to B", "Ctrl+Tab"),
          app.wait_for("Ctrl+Tab: B is active", "feriaBrowserHost",
                       "currentUrl", site + "/b", 15000),
          app.pause("Ctrl+Tab: B title settles", 700),
          title("Ctrl+Tab: B textarea preserved", "|value=%s|" % b)]
    b = prove_focus(s, "Ctrl+Shift+Tab", "b", values, "D")
    s += [app.key("Ctrl+Shift+Tab: cycle to C", "Ctrl+Shift+Tab"),
          app.wait_for("Ctrl+Shift+Tab: C active", "feriaBrowserHost",
                       "currentUrl", site + "/c", 15000),
          app.pause("Ctrl+Shift+Tab: C title settles", 700),
          title("Ctrl+Shift+Tab: C input preserved", "|value=%s|" % c)]
    c = prove_focus(s, "Ctrl+W", "c", values, "E")
    s += [app.key("Ctrl+W: close active tab C", "Ctrl+W"),
          app.wait_for("Ctrl+W: shell returns to HOME (15-second bound)",
                       "feriaAppPage", "viewState", "home", 15000),
          app.get("Ctrl+W: shell state after close",
                  "feriaAppPage", "viewState", "==", "home")]
    s += reopen_provider("Ctrl+W: reopen same provider")
    s += [app.get("Ctrl+W: surviving B address", "feriaBrowserHost",
                  "currentUrl", "contains", site + "/b"),
          app.pause("Ctrl+W: B title catch-up", 1000),
          title("Ctrl+W: surviving B title", "kbd=b|")]
    s += browser_menu("Ctrl+W: surviving tab", "Tabs", 1)
    s += close_menu("Ctrl+W: surviving tab")
    b = prove_focus(s, "Ctrl+Shift+T", "b", values, "F")
    s += [app.key("Ctrl+Shift+T: restore C", "Ctrl+Shift+T"),
          app.wait_for("Ctrl+Shift+T: restored C URL", "feriaBrowserHost",
                       "currentUrl", site + "/c", 15000),
          app.wait_for("Ctrl+Shift+T: C load complete", "feriaBrowserHost",
                       "loadSucceeded", True, 30000),
          app.pause("Ctrl+Shift+T: C title settles", 2500),
          title("Ctrl+Shift+T: C title marker", "kbd=c|")]
    s += browser_menu("Ctrl+Shift+T: restored tab", "Tabs", 2)
    s += close_menu("Ctrl+Shift+T: restored tab")
    c = prove_focus(s, "Ctrl+Shift+N", "c", values, "G")
    s += [app.key("Ctrl+Shift+N: open private tab", "Ctrl+Shift+N"),
          app.pause("Ctrl+Shift+N: private tab created", 1400)]
    s += address("private tab", site + "/private", "private")
    s += [*browser_menu("Ctrl+Shift+N: inspect private", "History"),
          app.get("Ctrl+Shift+N: private identity",
                  "feriaBrowserPrivacy", "text", "contains", "Private")]
    s += close_menu("Ctrl+Shift+N: private")
    s += [app.key("Ctrl+Shift+N: return to C to check original textarea",
                  "Ctrl+Shift+Tab"),
          app.wait_for("Ctrl+Shift+N: C survives", "feriaBrowserHost",
                       "currentUrl", site + "/c", 15000),
          app.pause("Ctrl+Shift+N: original title settles", 700),
          title("Ctrl+Shift+N: original textarea unchanged", "|value=%s|" % c)]
    s += app.QUIT
    return s


def single_close(site):
    s, values = app.boot(True), {}
    s += address("single tab", site + "/single", "single")
    text = prove_focus(s, "Ctrl+W ONLY tab", "single", values, "K")
    s += [app.key("Ctrl+W ONLY tab: close", "Ctrl+W"),
          app.wait_for("Ctrl+W ONLY tab: home", "feriaAppPage",
                       "viewState", "home", 15000)]
    s += reopen_provider("Ctrl+W ONLY tab: reopen provider")
    s += [app.get("Ctrl+W ONLY tab: newly opened provider home address",
                  "feriaBrowserHost", "currentUrl", "contains",
                  "mangaplus.shueisha.co.jp")]
    s += browser_menu("Ctrl+W ONLY tab: one fresh session", "Tabs", 1)
    s += close_menu("Ctrl+W ONLY tab: fresh menu")
    s += app.QUIT
    return s


def panels(site):
    s, values = app.boot(True), {}
    s += address("page tools", site + "/tools", "tools")
    for key, section, letter in (("Ctrl+H", "History", "H"),
                                 ("Ctrl+D", "Bookmarks", "D")):
        value = prove_focus(s, key, "tools", values, letter)
        s += [app.key(key + ": open panel", key),
              app.wait_for(key + ": panel visible", "feriaBrowser" + section + "List",
                           "visible", True, 10000)]
        s += unchanged(key, "tools", value)
        s += close_menu(key)
    value = prove_focus(s, "Ctrl+J", "tools", values, "J")
    s += [app.key("Ctrl+J: open Downloads", "Ctrl+J"),
          app.wait_for("Ctrl+J: panelOpen", "feriaRegularDownloads",
                       "panelOpen", True, 10000)]
    s += unchanged("Ctrl+J", "tools", value)
    s += [app.key("Ctrl+J: Escape close downloads", "Escape"),
          app.wait_for("Ctrl+J: panel dismissed", "feriaRegularDownloads",
                       "panelOpen", False, 10000)]
    value = prove_focus(s, "Ctrl+F", "tools", values, "F")
    s += [app.key("Ctrl+F: open Find", "Ctrl+F"),
          app.wait_for("Ctrl+F: Find input activeFocus", "feriaFindInput",
                       "activeFocus", True, 10000)]
    s += unchanged("Ctrl+F", "tools", value)
    s += [app.key("Ctrl+F: Escape", "Escape"),
          app.wait_for("Ctrl+F: Find closed", "feriaFindInput",
                       "visible", False, 10000),
          app.key("Ctrl+F: test focus restored without click (Q)", "Q"),
          app.pause("Ctrl+F: after Escape title", 650),
          observed("Ctrl+F: Escape page focus observation", "feriaBrowserHost", "pageTitle")]
    # Start a fresh page: Escape's focus result was recorded, not assumed.
    s += address("page zoom", site + "/zoom", "zoom")
    value = prove_focus(s, "Ctrl+=", "zoom", values, "I")
    s += [app.key("Ctrl+=: zoom in", "Ctrl+="),
          app.get("Ctrl+=: zoom greater than 1", "feriaQtWebEngine",
                  "zoomFactor", ">", 1)]
    s += unchanged("Ctrl+=", "zoom", value)
    value = prove_focus(s, "Ctrl+-", "zoom", values, "M")
    s += [app.key("Ctrl+-: zoom out", "Ctrl+-"),
          app.get("Ctrl+-: zoom back to 1", "feriaQtWebEngine",
                  "zoomFactor", "==", 1)]
    s += unchanged("Ctrl+-", "zoom", value)
    # Prepare reset without changing the tested textarea value.
    s += [*page_focus("Ctrl+0: prepare"), app.key("Ctrl+0: prepare zoom > 1", "Ctrl+="),
          app.get("Ctrl+0: prepared zoom", "feriaQtWebEngine", "zoomFactor", ">", 1)]
    value = prove_focus(s, "Ctrl+0", "zoom", values, "N")
    s += [app.key("Ctrl+0: reset zoom", "Ctrl+0"),
          app.get("Ctrl+0: zoom back to 1", "feriaQtWebEngine",
                  "zoomFactor", "==", 1)]
    s += unchanged("Ctrl+0", "zoom", value)
    value = prove_focus(s, "Ctrl+P", "zoom", values, "P")
    s += [app.key("Ctrl+P: request print", "Ctrl+P"),
          app.wait_for("Ctrl+P: print ticket pending", "feriaActiveBrowserRequests",
                       "printPromptPending", True, 10000)]
    s += unchanged("Ctrl+P", "zoom", value)
    s += [app.key("Ctrl+P: Escape cancel print", "Escape"),
          app.pause("Ctrl+P: print dialog dismissal", 1200),
          app.get("Ctrl+P: host remains after cancel", "feriaAppPage",
                  "viewState", "==", "host")]
    # FeriaActiveBrowserRequests changes objectName when its WebEngine becomes
    # invisible; querying it after FileDialog dismissal yields NO_SUCH_ITEM.
    # Do not mislabel that as a failed print cancellation. Ctrl+Q must still
    # exit gracefully, and no accepted print pathname is ever supplied.
    s += app.QUIT
    return s


def escape_focus(site):
    """Qualify the disputed Ctrl+L/Escape handoff without a page click."""
    s, values = app.boot(True), {}
    s += address("Escape control", site + "/escape", "escape")
    value = prove_focus(s, "Escape control", "escape", values, "A")
    s += [app.key("Ctrl+L Escape: open address", "Ctrl+L"),
          app.wait_for("Ctrl+L Escape: address active", "feriaBrowserAddress",
                       "activeFocus", True, 10000)]
    s += unchanged("Ctrl+L Escape", "escape", value)
    s += [app.key("Ctrl+L Escape: dismiss address", "Escape"),
          app.pause("Ctrl+L Escape: wait for focus return", 1000),
          app.key("Ctrl+L Escape: send B without page click", "B"),
          app.pause("Ctrl+L Escape: let page report", 1600),
          title("Ctrl+L Escape: AUTO keyboard return |value=ab|", "|value=ab|")]
    s += app.QUIT
    return s


def escape_click(site):
    """Separate launch proving whether an explicit click restores the caret."""
    s, values = app.boot(True), {}
    s += address("Escape click control", site + "/escape-click", "escape-click")
    value = prove_focus(s, "Escape click control", "escape-click", values, "A")
    s += [app.key("Ctrl+L Escape click: open address", "Ctrl+L"),
          app.wait_for("Ctrl+L Escape click: address active", "feriaBrowserAddress",
                       "activeFocus", True, 10000)]
    s += unchanged("Ctrl+L Escape click", "escape-click", value)
    s += [app.key("Ctrl+L Escape click: dismiss address", "Escape")]
    value = prove_focus(s, "Ctrl+L Escape click: explicit page click",
                        "escape-click", values, "B")
    s += unchanged("Ctrl+L Escape click: input after click", "escape-click", value)
    s += app.QUIT
    return s


SCENARIOS = {"navigation": navigation, "tabs": tabs, "panels": panels,
             "single-close": single_close, "escape-focus": escape_focus,
             "escape-click": escape_click}
DEFAULT_SCENARIOS = ("navigation", "tabs", "panels", "single-close")


def run(args, name, steps, tag):
    out = args.out / name
    out.mkdir(parents=True, exist_ok=True)
    scenario = out / "shortcuts.json"
    scenario.write_text(json.dumps({"name": "shortcuts-" + name, "steps": steps}, indent=1),
                        encoding="utf-8")
    log = out / "shortcuts.out"
    env = dict(os.environ)
    env["PATH"] = str(args.qt_bin) + os.pathsep + env.get("PATH", "")
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    cmd = [str(args.lanista), "session", "run", str(scenario),
           "--exe", str(args.exe), "--qml", "qml/Main.qml", "--drive",
           "--tag", tag]
    # DO NOT add --keep-going: a failed viewState guard MUST prevent a
    # subsequent ui-click from landing on a home-screen provider tile.
    moments = {}
    def pump(stream, capture):
        for line in stream:
            stamp = time.monotonic()
            capture.write(line)
            capture.flush()
            if line.startswith("PASS  Ctrl+W: close active tab C"):
                moments["two_key"] = stamp
            elif line.startswith("PASS  Ctrl+W: shell returns to HOME"):
                moments["two_home"] = stamp
            elif line.startswith("PASS  Ctrl+W ONLY tab: close"):
                moments["single_key"] = stamp
            elif line.startswith("PASS  Ctrl+W ONLY tab: home"):
                moments["single_home"] = stamp
    with log.open("w", encoding="utf-8", errors="replace") as capture:
        proc = subprocess.Popen(cmd, cwd=app.ROOT, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace", bufsize=1)
        reader = threading.Thread(target=pump, args=(proc.stdout, capture), daemon=True)
        reader.start()
        try:
            code = proc.wait(timeout=240)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)
            code = -1
        reader.join(timeout=10)
    timings = {kind: round(moments[kind + "_home"] -
                           moments[kind + "_key"], 3)
               for kind in ("two", "single")
               if kind + "_key" in moments and kind + "_home" in moments}
    (out / "close-timings.json").write_text(
        json.dumps({"seconds_key_to_home_log": timings,
                    "note": "Timestamped Lanista PASS lines; includes command/transport latency."},
                   indent=2), encoding="utf-8")
    for kind, seconds in timings.items():
        print("CTRL+W", kind, "key-to-home observed-log seconds", seconds, flush=True)
    output = log.read_text(encoding="utf-8", errors="replace")
    summary = re.search(r"(\d+) steps, (\d+) failed", output)
    manifest = re.search(r"manifest: (.*?)\)", output)
    state = {}
    if manifest:
        manifest_path = app.ROOT / manifest.group(1)
        if manifest_path.exists():
            state = json.loads(manifest_path.read_text(encoding="utf-8"))
    clean = (state.get("exitCode") == 0 and state.get("crashed") is False
             and state.get("killReason") == "graceful")
    ok = bool(summary) and summary.group(2) == "0" and clean and code == 0
    print(("PASS" if ok else "FAIL"), name,
          summary.group(0) if summary else "no step summary",
          "lanista-exit", code, "app-exit", state.get("exitCode"),
          "crashed", state.get("crashed"), "killReason", state.get("killReason"),
          "log", log, flush=True)
    for line in output.splitlines():
        if line.startswith(("FAIL", "INFRA")):
            print("   " + line[:450], flush=True)
    return ok


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--lanista", required=True, type=Path)
    parser.add_argument("--qt-bin", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--scenario", choices=[*SCENARIOS, "all"], default="all")
    args = parser.parse_args()
    args.exe, args.lanista = args.exe.resolve(), args.lanista.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), KeyboardSite)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    site = "http://127.0.0.1:%d" % server.server_address[1]
    try:
        names = list(DEFAULT_SCENARIOS) if args.scenario == "all" else [args.scenario]
        results = []
        for name in names:
            tag = ("feria-set4-0008-" + name + "-" +
                   time.strftime("%Y%m%d-%H%M%S"))
            results.append(run(args, name, SCENARIOS[name](site), tag))
        passed = all(results)
    finally:
        server.shutdown()
    print("SHORTCUTS PROOF", "PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
