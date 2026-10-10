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


def unchanged(shortcut, text):
    return [app.pause(shortcut + ": deliver page title", 300),
            title(shortcut + ": textarea value unchanged", "|value=" + text + "|")]


def page_focus(label):
    return app.click(label + ": give textarea the keyboard", "feriaQtWebEngine")


def address(label, url):
    return [app.click(label + ": open address", "feriaBrowserAddressToggle"),
            app.pause(label + ": address ready", 350),
            *app.typed(label + ": enter local URL", "feriaBrowserAddress", url),
            app.key(label + ": navigate", "Return"),
            app.pause(label + ": navigation settles", 800),
            app.wait_for(label + ": loading completed", "feriaBrowserHost",
                         "loadSucceeded", True, 30000),
            app.get(label + ": local address", "feriaBrowserHost",
                    "currentUrl", "contains", url)]


def menu_open(label, sequence, section):
    return [page_focus(label), app.key(label, sequence),
            app.wait_for(label + ": " + section + " is visible",
                         "feriaBrowser" + section + "List", "visible", True, 10000)]


def close_menu(label):
    return [app.click(label + ": dismiss browser menu", "feriaBrowserMenuDismiss"),
            app.pause(label + ": modal closes", 300)]


def steps(site):
    a, b, c, private = (site + "/" + part for part in ("a", "b", "c", "private"))
    s = app.boot(True)

    # Control: a plain key really enters the page, rather than just the app.
    s += address("load keyboard fixture A", a)
    s += [page_focus("initial page"), app.key("CONTROL: plain A to web textarea", "A"),
          app.pause("CONTROL: input callback updates title", 400),
          title("CONTROL: WebEngine received plain A", "kbd=a|loads=1|value=a|keydown=")]

    s += [page_focus("Ctrl+L"), app.key("Ctrl+L: address editor", "Ctrl+L"),
          app.wait_for("Ctrl+L: editor has active focus",
                       "feriaBrowserAddress", "activeFocus", True)]
    s += unchanged("Ctrl+L", "a")
    s += [app.key("Ctrl+L: Escape", "Escape"), page_focus("after Escape"),
          app.key("Ctrl+L: page receives B again", "B"),
          app.pause("Ctrl+L: resumed typing reported", 300),
          title("Ctrl+L: focus returns to textarea", "|value=ab|")]

    s += [page_focus("Ctrl+R"), app.key("Ctrl+R: reload", "Ctrl+R"),
          app.pause("Ctrl+R: navigation result", 1000),
          title("Ctrl+R: load count becomes 2", "kbd=a|loads=2|")]
    s += unchanged("Ctrl+R", "ab")
    s += [page_focus("F5"), app.key("F5: reload", "F5"),
          app.pause("F5: navigation result", 1000),
          title("F5: load count becomes 3", "kbd=a|loads=3|")]
    s += unchanged("F5", "ab")

    s += address("load keyboard fixture B", b)
    s += [page_focus("B navigation"), app.key("CONTROL: enter C on B", "C"),
          app.pause("CONTROL: B input reported", 300),
          title("CONTROL: B has the keyboard", "kbd=b|loads=1|value=c|")]
    s += [page_focus("Alt+Left"), app.key("Alt+Left: back", "Alt+Left"),
          app.pause("Alt+Left: page completes", 900),
          app.get("Alt+Left: prior URL", "feriaBrowserHost", "currentUrl", "contains", "/a"),
          title("Alt+Left: prior page remains intact", "kbd=a|")]
    s += unchanged("Alt+Left", "ab")
    s += [page_focus("Alt+Right"), app.key("Alt+Right: forward", "Alt+Right"),
          app.pause("Alt+Right: page completes", 900),
          app.get("Alt+Right: forward URL", "feriaBrowserHost", "currentUrl", "contains", "/b"),
          title("Alt+Right: returns to B", "kbd=b|")]
    s += unchanged("Alt+Right", "c")

    s += [page_focus("Ctrl+T"), app.key("Ctrl+T: create regular tab", "Ctrl+T"),
          app.pause("Ctrl+T: new tab created", 1500),
          app.key("Ctrl+T: inspect tabs via History", "Ctrl+H"),
          app.wait_for("Ctrl+T: tabs count two", "feriaBrowserTabsList", "count", 2, 10000)]
    s += close_menu("after Ctrl+T")
    s += address("new tab goes to local fixture C", c)
    s += [page_focus("new tab C"), app.key("CONTROL: C gets D", "D"),
          app.pause("CONTROL: C title updated", 300),
          title("CONTROL: new tab C has keyboard", "kbd=c|loads=1|value=d|")]
    s += [page_focus("Ctrl+W"), app.key("Ctrl+W: close tab C", "Ctrl+W"),
          app.pause("Ctrl+W: return to B", 1200),
          app.get("Ctrl+W: restored B URL", "feriaBrowserHost", "currentUrl", "contains", "/b")]
    s += unchanged("Ctrl+W", "c")
    s += [page_focus("Ctrl+Shift+T"), app.key("Ctrl+Shift+T: reopen C", "Ctrl+Shift+T"),
          app.pause("Ctrl+Shift+T: reopened tab load", 1400),
          app.get("Ctrl+Shift+T: restored C URL", "feriaBrowserHost", "currentUrl", "contains", "/c"),
          title("Ctrl+Shift+T: browser displays C", "kbd=c|")]
    s += [page_focus("Ctrl+Tab"), app.key("Ctrl+Tab: cycle to B", "Ctrl+Tab"),
          app.pause("Ctrl+Tab: tab activation", 600),
          app.get("Ctrl+Tab: B active", "feriaBrowserHost", "currentUrl", "contains", "/b")]
    s += unchanged("Ctrl+Tab", "c")
    s += [page_focus("Ctrl+Shift+Tab"), app.key("Ctrl+Shift+Tab: cycle to C", "Ctrl+Shift+Tab"),
          app.pause("Ctrl+Shift+Tab: tab activation", 600),
          app.get("Ctrl+Shift+Tab: C active", "feriaBrowserHost", "currentUrl", "contains", "/c")]
    s += [page_focus("close extra tab"), app.key("close C after tab cycling", "Ctrl+W"),
          app.pause("closed C", 600),
          app.get("B active again", "feriaBrowserHost", "currentUrl", "contains", "/b")]
    s += unchanged("Ctrl+Shift+Tab", "c")

    s += [page_focus("Ctrl+Shift+N"), app.key("Ctrl+Shift+N: private tab", "Ctrl+Shift+N"),
          app.pause("Ctrl+Shift+N: tab created", 1200)]
    s += address("private tab loopback fixture", private)
    s += [page_focus("private page"), app.key("CONTROL: private receives P", "P"),
          app.pause("CONTROL: private input", 300),
          title("CONTROL: private page keyboard", "kbd=private|loads=1|value=p|"),
          page_focus("private menu"), app.key("Ctrl+Shift+N: inspect private mode", "Ctrl+H"),
          app.wait_for("private History visible", "feriaBrowserHistoryList", "visible", True),
          app.get("Ctrl+Shift+N: private identity", "feriaBrowserPrivacy", "text", "contains", "Private")]
    s += close_menu("private")
    s += unchanged("Ctrl+Shift+N", "p")
    s += [page_focus("private close"), app.key("close private tab", "Ctrl+W"),
          app.pause("private tab closed", 600),
          app.get("regular B returns", "feriaBrowserHost", "currentUrl", "contains", "/b")]

    for label, sequence, section in (("Ctrl+H", "Ctrl+H", "History"),
                                     ("Ctrl+D", "Ctrl+D", "Bookmarks")):
        s += menu_open(label, sequence, section)
        s += close_menu(label)
        s += unchanged(label, "c")

    s += [page_focus("Ctrl+J"), app.key("Ctrl+J: downloads panel", "Ctrl+J"),
          app.wait_for("Ctrl+J: downloads panel opens",
                       "feriaRegularDownloads", "panelOpen", True, 10000),
          app.key("Ctrl+J: close empty downloads", "Escape"),
          app.wait_for("Ctrl+J: downloads dismissed",
                       "feriaRegularDownloads", "panelOpen", False, 10000)]
    s += unchanged("Ctrl+J", "c")

    s += [page_focus("Ctrl+F"), app.key("Ctrl+F: find text", "Ctrl+F"),
          app.wait_for("Ctrl+F: find input has focus", "feriaFindInput", "activeFocus", True),
          app.key("Ctrl+F: Escape dismisses Find", "Escape"),
          app.wait_for("Ctrl+F: panel is dismissed", "feriaFindInput", "visible", False),
          app.pause("Ctrl+F: page focus restored", 350),
          app.key("Ctrl+F: resumed page input E", "E"),
          app.pause("Ctrl+F: page accepted resumed typing", 400),
          title("Ctrl+F: textarea regained keyboard", "|value=ce|")]

    s += [page_focus("Ctrl+="), app.key("Ctrl+=: zoom in", "Ctrl+="),
          app.get("Ctrl+=: engine zoomed in", "feriaQtWebEngine", "zoomFactor", ">", 1)]
    s += unchanged("Ctrl+=", "ce")
    s += [page_focus("Ctrl+-"), app.key("Ctrl+-: zoom out", "Ctrl+-"),
          app.get("Ctrl+-: engine zoom reset to 100%", "feriaQtWebEngine", "zoomFactor", "==", 1)]
    s += unchanged("Ctrl+-", "ce")
    s += [page_focus("zoom up before reset"), app.key("prepare Ctrl+0: zoom in", "Ctrl+="),
          page_focus("Ctrl+0"), app.key("Ctrl+0: reset zoom", "Ctrl+0"),
          app.get("Ctrl+0: engine zoom is 100%", "feriaQtWebEngine", "zoomFactor", "==", 1)]
    s += unchanged("Ctrl+0", "ce")

    # Last: native save dialog may retain focus in offscreen mode. Cancel only;
    # no filename is chosen and no printToPdf call is issued.
    s += [page_focus("Ctrl+P"), app.key("Ctrl+P: request print", "Ctrl+P"),
          app.wait_for("Ctrl+P: print picker ticket allocated",
                       "feriaActiveBrowserRequests", "printPromptPending", True, 10000)]
    s += unchanged("Ctrl+P", "ce")
    s += [app.key("Ctrl+P: cancel save dialog", "Escape"),
          app.wait_for("Ctrl+P: print request cancelled",
                       "feriaActiveBrowserRequests", "printPromptPending", False, 10000)]
    s += app.QUIT
    return s


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--lanista", required=True, type=Path)
    parser.add_argument("--qt-bin", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--smoke-steps", type=int, default=0,
                        help="Diagnostics only: stop at this step and attempt graceful Quit")
    args = parser.parse_args()
    args.exe, args.lanista = args.exe.resolve(), args.lanista.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), KeyboardSite)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    site = "http://127.0.0.1:%d" % server.server_address[1]
    try:
        all_steps = steps(site)
        if args.smoke_steps:
            all_steps = all_steps[:args.smoke_steps] + app.QUIT
        scenario = args.out / "shortcuts.json"
        scenario.write_text(json.dumps({"name": "shortcuts", "steps": all_steps}, indent=1),
                            encoding="utf-8")
        log = args.out / "shortcuts.out"
        env = dict(os.environ)
        env["PATH"] = str(args.qt_bin) + os.pathsep + env["PATH"]
        env["QT_QPA_PLATFORM"] = "offscreen"
        tag = "feria-set4-shortcuts-" + time.strftime("%Y%m%d-%H%M%S")
        cmd = [str(args.lanista), "session", "run", str(scenario), "--exe", str(args.exe),
               "--qml", "qml/Main.qml", "--drive", "--keep-going", "--tag", tag]
        with log.open("w", encoding="utf-8", errors="replace") as capture:
            proc = subprocess.Popen(cmd, cwd=app.ROOT, env=env, stdout=capture,
                                    stderr=subprocess.STDOUT)
            try:
                exit_code = proc.wait(timeout=240)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=10)
                exit_code = -1
        output = log.read_text(encoding="utf-8", errors="replace")
        summary = re.search(r"(\d+) steps, (\d+) failed", output)
        manifest = re.search(r"manifest: (.*?)\)", output)
        state = json.loads((app.ROOT / manifest.group(1)).read_text()) if manifest else {}
        clean = (state.get("exitCode") == 0 and state.get("crashed") is False
                 and state.get("killReason") == "graceful")
        ok = bool(summary) and summary.group(2) == "0" and clean and exit_code == 0
        print("LANISTA", summary.group(0) if summary else "no step summary",
              "process exit", exit_code, "app exit", state.get("exitCode"),
              "crashed", state.get("crashed"), "killReason", state.get("killReason"),
              "log", log, flush=True)
        for line in output.splitlines():
            if line.startswith(("FAIL", "INFRA")):
                print(line[:400], flush=True)
    finally:
        server.shutdown()
    print("SHORTCUTS PROOF", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
