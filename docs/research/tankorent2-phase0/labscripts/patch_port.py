"""Pin the lab specimen off port 11470 (Phase-0 safety spine).

WHY: Colosseum's StreamServer adopt-firsts whatever answers on 127.0.0.1:11470
(native/player/streamserver.cpp:79). A lab server there would silently capture real
playback, or collide and break streaming. server.js has NO port env knob - verified
by enumerating every process.env read in the bundle; none influences the port.

WHAT: every literal 11470 becomes 11480. Six occurrences, all one byte ('7'->'8'),
so byte offsets never shift and the pristine copy stays diffable against this one.

  1 EngineFS listen block  `http.createServer(app)), port = 11470`  - THE listener
  2 usenet module default  `let ip = "127.0.0.1", port = 11470`     - outbound ref
  3 hls-converter fallback `serverPort = serverPort || 11470`       - outbound ref
  4 subtitles fetch        `http://127.0.0.1:11470/subtitles.srt`   - outbound ref
  5 local addon engineUrl  `let engineUrl = "http://127.0.0.1:11470"` - outbound ref
  6 CORS origin regex      `(127.0.0.1|localhost):11470$`           - inbound check

Occurrences 2-5 matter for ISOLATION, not just tidiness: left at 11470 they would
reach out to the REAL Stremio service whenever it happens to be running.

NOTE on the retry band: the listen block's fallback is `port++ < 11474`. Starting at
11480 that predicate is false immediately, so a busy port fails LOUDLY instead of
walking the lab back toward the production range. Deliberate.
"""
import sys

OLD = b"11470"
NEW = b"11480"
EXPECT = 6

path = sys.argv[1]
data = open(path, "rb").read()

hits = data.count(OLD)
if hits != EXPECT:
    sys.exit("FAIL: expected %d occurrences of %s, found %d - refusing to patch"
             % (EXPECT, OLD.decode(), hits))
if NEW in data:
    sys.exit("FAIL: %s already present in the specimen - would be ambiguous"
             % NEW.decode())

open(path, "wb").write(data.replace(OLD, NEW))
print("OK: %d occurrences %s -> %s (1 byte each, no offset shift)"
      % (hits, OLD.decode(), NEW.decode()))
