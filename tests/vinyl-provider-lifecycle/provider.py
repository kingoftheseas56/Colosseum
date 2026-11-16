"""Synthetic NDJSON helper. PID/sequence expose lifecycle; never uses a network."""
import json
import os
import sys
import time

sequence = 0
if os.environ.get("VINYL_FAKE_START_LOG"):
    with open(os.environ["VINYL_FAKE_START_LOG"], "a", encoding="utf-8") as starts:
        starts.write(str(os.getpid()) + "\n")

def answer(raw):
    global sequence
    request = json.loads(raw)
    sequence += 1
    operation = request["operation"]
    value = request.get("value", "")
    if operation == "mediaWarmup":
        time.sleep(float(os.environ.get("VINYL_FAKE_WARM_SECONDS", "0")))
    if value == "delay":
        time.sleep(0.55)
    if value == "slow-search":
        time.sleep(2)
    if value == "error":
        return {"ok": False, "error": "Synthetic failure"}
    if value == "exit":
        sys.exit(7)
    if value == "malformed":
        return {"invalid": True}
    return {"ok": True, "data": {"pid": os.getpid(), "sequence": sequence,
                                    "operation": operation, "value": value}}

if "--serve" in sys.argv:
    for raw in sys.stdin.buffer:
        response = answer(raw)
        sys.stdout.write(json.dumps(response) + "\n")
        sys.stdout.flush()
else:
    sys.stdout.write(json.dumps(answer(sys.stdin.buffer.read())))
