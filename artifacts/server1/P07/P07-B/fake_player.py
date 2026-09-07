import json
import os
import sys
import time
from pathlib import Path

if "--version" in sys.argv:
    print("fake-player 7.2.1")
    raise SystemExit(0)

events = [
    {"event": "valid_media_decode", "at_ms": 20},
    {"event": "presented_frame", "at_ms": 30},
    {"event": "seek_complete", "at_ms": 60, "target_seconds": 12.5},
    {"event": "stall", "at_ms": 80, "duration_ms": 40},
]
event_file = Path(os.environ["COLOSSEUM_PLAYER_PROBE_EVENT_FILE"])
time.sleep(0.05)
event_file.write_text("".join(json.dumps(event) + "\n" for event in events), encoding="utf-8")
time.sleep(0.2)
