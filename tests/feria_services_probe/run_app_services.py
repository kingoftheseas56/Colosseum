"""Account-free Set 5 checks through the full app and its Lanista interface.

Two launches use one disposable profile and one loopback site. The fixture
seeds its own cookie; no provider credentials or personal profile are used.
"""
import argparse
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "feria_history_probe"))
import run_app_restart as app


class Site(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        route = self.path.split("?", 1)[0]
        signed_in = "feria_services=fixture" in self.headers.get("Cookie", "")
        headers = []
        script = ""
        if route == "/seed":
            headers = [("Set-Cookie", "feria_services=fixture; Max-Age=86400; Path=/; SameSite=Lax")]
            title = "service:cookie-seeded"
        elif route == "/check":
            title = "service:signed-in" if signed_in else "service:signed-out"
        elif route == "/notifications":
            title = "service:notification-pending"
            script = ("Notification.requestPermission().then(function(value){"
                      "document.title='service:notifications:'+value})")
        else:
            title = "service:ready"
        body = ("<!doctype html><title>" + title + "</title><h1>" + title
                + "</h1><script>" + script + "</script>").encode()
        self.send_response(200)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def go(label, url, title):
    return [app.click(label + ": address", "feriaBrowserAddressToggle"),
            app.pause(label + ": address opens", 350),
            *app.typed(label + ": type URL", "feriaBrowserAddress", url),
            app.key(label + ": navigate", "Return"),
            app.wait_for(label + ": URL", "feriaBrowserHost", "currentUrl", url, 20000),
            app.wait_for(label + ": site result", "feriaBrowserHost", "pageTitle", title, 20000)]


def notifications(site, label):
    return [*go(label, site + "/notifications", "service:notifications:denied"),
            app.get(label + ": no permission request", "feriaActiveBrowserRequests",
                    "current", "==", "null")]


def boot(first):
    # The first tile is visible at every supported column count. Avoid the
    # older helper's fixed three-column keyboard route to an offscreen tile.
    steps = app.boot(first)
    end = next(i for i, step in enumerate(steps) if step.get("label") == "to tile 2")
    return steps[:end] + [
        app.click("open the first visible browser tile", "feriaApp_netflix"),
        app.wait_for("browser host is showing", "feriaAppPage", "viewState", "host", 15000),
        app.wait_for("browser request owner is ready", "feriaActiveBrowserRequests", "active", True, 15000)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--lanista", required=True, type=Path)
    parser.add_argument("--qt-bin", required=True, type=Path)
    parser.add_argument("--platform", choices=("windows", "offscreen"), default="windows")
    parser.add_argument("--out", type=Path, default=app.ROOT / "artifacts" / "feria-set5-services")
    args = parser.parse_args()
    args.exe, args.lanista = args.exe.resolve(), args.lanista.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Site)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    site = "http://127.0.0.1:%d" % server.server_address[1]
    tag = "feria-set5-" + time.strftime("%Y%m%d-%H%M%S")
    first_steps = boot(True) + [
        *go("seed fixture cookie", site + "/seed", "service:cookie-seeded"),
        *go("regular cookie", site + "/check", "service:signed-in"),
        *notifications(site, "ordinary notifications"),
        app.key("open private tab", "Ctrl+Shift+N"),
        app.pause("private tab opens", 1500),
        *go("private cookie isolation", site + "/check", "service:signed-out"),
        *notifications(site, "private notifications"),
    ] + app.QUIT
    second_steps = boot(False) + [
        *go("cookie after process restart", site + "/check", "service:signed-in"),
        *notifications(site, "notifications after restart"),
    ] + app.QUIT
    try:
        first = app.run(args, "services-first-launch", first_steps, tag, args.out)
        second = first and app.run(args, "services-second-launch", second_steps, tag, args.out)
    finally:
        server.shutdown()
        server.server_close()
    print("ACCOUNT-FREE SERVICES PROOF", "PASS" if first and second else "FAIL")
    return 0 if first and second else 1


if __name__ == "__main__":
    sys.exit(main())
