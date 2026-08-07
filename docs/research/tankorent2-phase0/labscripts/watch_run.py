"""Watch the lab engine run and record what it actually does.

Re-scoped by Slice 2's finding: getStatistics already returns `unique` (known peers),
`sources` (per-source DHT/tracker discovery counts), `queued`, `connectionTries`,
`swarmSize`, `swarmConnections` and a per-peer `wires[]` array. Colosseum reads six
fields and ignores the rest, so discovered-vs-connected and per-source attribution
need NO engine patching - we just ask for what it already offers.

Drives playback with a direct ranged GET (never through Colosseum) and samples
stats.json on the app's own 1 Hz cadence so the numbers are comparable to what the
player displays.

Usage:
  python watch_run.py --port 11480 --hash <infohash> --idx <n> --secs 120 --tag cold
  python watch_run.py ... --no-trackers      (negative control: DHT-only discovery)
  python watch_run.py ... --settings '{"btMaxConnections":5}'  (control: knob landed)

Writes <outdir>/<tag>.jsonl (one sample per second) and prints a summary.
Exit 0 on a completed run, 2 if the engine never produced a first byte.
"""
import argparse
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request

TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.demonii.com:1337/announce",
    "udp://tracker.torrent.eu.org:451/announce",
]


def http(url, data=None, headers=None, timeout=10):
    req = urllib.request.Request(url, data=data, headers=headers or {})
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def create_engine(port, ih, use_trackers):
    """POST /:hash/create. Sources control discovery: dht: and tracker: only."""
    sources = ["dht:" + ih]
    if use_trackers:
        sources += ["tracker:" + t for t in TRACKERS]
    body = json.dumps({"sources": sources}).encode()
    return http("http://127.0.0.1:%d/%s/create" % (port, ih), data=body, timeout=30)


class Puller(threading.Thread):
    """Drive real streaming with a ranged GET, like mpv would."""

    def __init__(self, port, ih, idx):
        super().__init__(daemon=True)
        self.url = "http://127.0.0.1:%d/%s/%d" % (port, ih, idx)
        self.first_byte_at = None
        self.bytes = 0
        self.err = None
        self.stop = threading.Event()

    def run(self):
        t0 = time.monotonic()
        try:
            req = urllib.request.Request(self.url, headers={"Range": "bytes=0-"})
            with urllib.request.urlopen(req, timeout=300) as r:
                while not self.stop.is_set():
                    chunk = r.read(65536)
                    if not chunk:
                        break
                    if self.first_byte_at is None:
                        self.first_byte_at = time.monotonic() - t0
                    self.bytes += len(chunk)
        except Exception as e:               # noqa: BLE001 - report, never crash the run
            self.err = repr(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=11480)
    ap.add_argument("--hash", required=True)
    ap.add_argument("--idx", type=int, default=0)
    ap.add_argument("--secs", type=int, default=120)
    ap.add_argument("--tag", default="run")
    ap.add_argument("--outdir", default=".")
    ap.add_argument("--no-trackers", action="store_true")
    ap.add_argument("--settings", default=None)
    ap.add_argument("--no-pull", action="store_true")
    a = ap.parse_args()

    ih = a.hash.lower()
    os.makedirs(a.outdir, exist_ok=True)
    path = os.path.join(a.outdir, a.tag + ".jsonl")

    if a.settings:
        st, _ = http("http://127.0.0.1:%d/settings" % a.port,
                     data=a.settings.encode())
        print("settings POST -> %s  %s" % (st, a.settings))

    # record the settings the engine will actually use, so a claim about a knob
    # can be checked against what the engine reported, not what we intended.
    _, raw = http("http://127.0.0.1:%d/settings" % a.port)
    effective = json.loads(raw).get("values", {})

    t_create = time.monotonic()
    st, _ = create_engine(a.port, ih, not a.no_trackers)
    create_ms = (time.monotonic() - t_create) * 1000
    print("create -> HTTP %s in %.0f ms (trackers=%s)"
          % (st, create_ms, not a.no_trackers))

    puller = None
    if not a.no_pull:
        puller = Puller(a.port, ih, a.idx)
        puller.start()

    t0 = time.monotonic()
    samples = []
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps({"_meta": {
            "tag": a.tag, "hash": ih, "idx": a.idx, "port": a.port,
            "trackers": not a.no_trackers, "settings_posted": a.settings,
            "effective_bt": {k: v for k, v in effective.items()
                             if k.startswith("bt") or k == "cacheSize"},
        }}) + "\n")
        while time.monotonic() - t0 < a.secs:
            time.sleep(1)
            try:
                _, raw = http("http://127.0.0.1:%d/%s/%d/stats.json"
                              % (a.port, ih, a.idx), timeout=5)
                s = json.loads(raw)
            except Exception as e:           # noqa: BLE001
                f.write(json.dumps({"t": round(time.monotonic() - t0, 1),
                                    "error": repr(e)}) + "\n")
                continue
            if not s:
                continue
            row = {
                "t": round(time.monotonic() - t0, 1),
                "peers": s.get("peers"),
                "unchoked": s.get("unchoked"),
                "unique": s.get("unique"),
                "queued": s.get("queued"),
                "tries": s.get("connectionTries"),
                "swarmSize": s.get("swarmSize"),
                "swarmConnections": s.get("swarmConnections"),
                "downloaded": s.get("downloaded"),
                "downloadSpeed": s.get("downloadSpeed"),
                "streamProgress": s.get("streamProgress"),
                "peerSearchRunning": s.get("peerSearchRunning"),
                "sources": s.get("sources"),
            }
            samples.append(row)
            f.write(json.dumps(row) + "\n")
        if puller:
            puller.stop.set()

    if not samples:
        print("FAIL: no samples")
        return 1

    peak = max(samples, key=lambda r: (r["peers"] or 0))
    last = samples[-1]
    print("\n--- %s ---" % a.tag)
    print("  effective btMaxConnections=%s btMinPeersForStable=%s"
          % (effective.get("btMaxConnections"), effective.get("btMinPeersForStable")))
    print("  peak connected peers : %s   (at t=%ss)" % (peak["peers"], peak["t"]))
    print("  peak KNOWN peers     : %s" % max((r["unique"] or 0) for r in samples))
    print("  peak unchoked        : %s" % max((r["unchoked"] or 0) for r in samples))
    print("  final downloaded     : %s bytes" % last["downloaded"])
    print("  peak downloadSpeed   : %.0f B/s"
          % max((r["downloadSpeed"] or 0) for r in samples))
    print("  final streamProgress : %s" % last["streamProgress"])
    if puller:
        print("  first byte to client : %s"
              % ("%.2f s" % puller.first_byte_at if puller.first_byte_at
                 else "NEVER (%s)" % puller.err))
        print("  bytes pulled         : %s" % puller.bytes)
    print("  sources (final)      : %s" % json.dumps(last.get("sources")))
    print("  log: %s" % path)

    if puller and puller.first_byte_at is None:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
