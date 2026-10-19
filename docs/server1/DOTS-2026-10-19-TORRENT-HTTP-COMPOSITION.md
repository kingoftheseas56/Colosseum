# Native torrent HTTP composition checkpoint

The standalone host now runs the production Colosseum Server 1 composition with
an HTTP listener, EngineRegistry, source stream leases, deferred metadata responses,
native torrent transport, storage and FileReader. The default smoke command starts
and stops the real composition; --serve keeps its Qt event loop running.

## Implemented and verified slice

- Torrent full-file/range/HEAD serving, filename MIME mapping and source malformed
  Range fallback. Cross-piece data is checked against different K/L piece bytes.
- Hash, uploaded metainfo blob, local metainfo file and HTTP metainfo URL create
  paths; source fetch.buffer behavior accepts a valid metainfo body even on HTTP404.
- Measured source-shaped statistics and engine remove controls.
- Safe deferred response retirement for canceled metadata acquisition, reader
  disconnect and server shutdown. CRLF response header injection rejected.
- PeerSearch policy dispatch plumbing, source duplicate delivery and constructor
  timer phase. Concrete live network acquisition is not mounted in this checkpoint.
- M01 probe component registered in the full native build and test suite.

## Evidence

Parent independently ran all 115 registered tests: 115 passed, no failures,
51.81 seconds, using the explicit fixed-interface native transport test profile,
PowerShell on PATH and writable HOME/XDG directories. Full output accompanies this
report. A separate independent real peer-to-HTTP run also passed.

Real transfer fixture traverses the native adapter, scheduler/store, FileReader
and HTTP socket, serving 32768 distinct K/L bytes and ranges crossing byte16384.
It aborts one reader while its sibling continues, checks headers/statistics/control
routes and leaves a metadata response pending during normal shutdown. The host
exits0. This is controlled localhost transfer, not public swarm or player playback.

Statistics projection independently matches 13 executed M172 cases from the pinned
oracle. Range projection independently matches524 executed M176 cases. Engine
statistics retain known PWS lifecycle gaps documented in the separate K12 receipt.

## Remaining requirements

Complete H01 filter/tracker/sys-stat/guess-file behavior and exact24h socket inactivity
timeout remain. Live tracker/DHT acquisition, N00 proxy, additional media/subtitle/HLS
and archive services are separately in progress. Embedding in the desktop application,
Windows, actual player playback and full unmodified-reference raw HTTP comparison
remain unqualified. The cloud's Node interface enumeration restriction still blocks
full reference startup; factory differential tests do not replace that gate.

No production merge, release or total1:1-parity claim.
