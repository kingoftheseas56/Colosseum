"""One-shot, file-backed transport fixture. Never imports a network client."""
import json
import os
from pathlib import Path
import sys
import time


request = json.load(sys.stdin)
state = json.loads(Path(os.environ["VINYL_POOL_TEST_STATE"]).read_text(encoding="utf-8"))
log_path = Path(state["logPath"])


def record(event):
    with log_path.open("a", encoding="utf-8") as log:
        log.write(json.dumps({"event": event, "atNs": time.monotonic_ns(),
                              "pid": os.getpid(), "request": request}, ensure_ascii=True) + "\n")


record("start")
value = request.get("value", "")
time.sleep(state.get("delays", {}).get(value, state.get("delayMs", 0)) / 1000)
response = state.get("responses", {}).get(value, state.get("response"))
record("finish")
if response is None:
    json.dump({"ok": False, "error": "No recorded response for this fixture request."}, sys.stdout)
else:
    json.dump({"ok": True, "data": response}, sys.stdout, ensure_ascii=True)
