# Live discovery, proxy, subtitles and source-owned peer lifecycle

Incremental verified integration, not full Stremio server parity.

Production composition now installs live tracker/DHT discovery and N00 proxy plus
M02 subtitle/track/hash routes before the torrent fallback. The host's explicit
--no-discovery option keeps controlled manual-peer fixtures local; it is not confused
with the separate fixed-interface environment qualifier. Public tr overrides are
source-shaped. HTTP inactivity timeout follows the source 24-hour socket inactivity
behavior, refreshed by traffic rather than an absolute request deadline.

M818 source-owned dial admission, queue/capacity, timeouts and reconnect waits now
control actual pinned libtorrent2.0.11 network-lane operations. Autonomous native
dial scheduling is disabled. No vendor source patches or version changes were made.

## Fresh parent evidence

- Full registered suite:130/130 passed, no failures,79.22 seconds. Explicit
  SERVER1_TEST_FIXED_INTERFACE=1, PowerShell PATH and writable HOME/XDG profile.
- Independent real tracker-to-peer-to-HTTP fixture passed with no manual peer
  address injection; distinct K/L pieces and cross-piece ranges, abort-sibling
  behavior and normal shutdown were verified.
- Independent discovery focused suite:9/9 passed, including pinned differential.
- Independent N00 proxy lifecycle and28 pinned source differential cases passed.
- Independent M02 unit/parser/hash and1,220 charset fixtures passed. Independent
  lifecycle run:41 passed including17 exact source I/O comparisons.
- Frozen source SHA manifests were verified before committing.

Worker receipts retain Address/UndefinedBehaviorSanitizer evidence and precise
factory/module identities. LeakSanitizer is unavailable in this ptrace environment.
Test media are synthetic blue-frame/sine fixtures, now explicitly versioned so a
fresh checkout can run the registered lifecycle tests without an unpublished file.

## Remaining limits

H01 regex filtering/guessing/sys-stat and some removal behavior remain in progress.
Unknown inbound native peer association and raw source address validation remain
explicit lifecycle gaps. HLS/sample/conversion services and other ancillary/archive
capabilities are still under implementation. Desktop embedding, Windows, player
playback, public-swarm soak and full unchanged-reference raw-wire qualification are
not established. The Node network-interface restriction still blocks full oracle
startup in this cloud.

N00 deliberately retains TLS certificate validation and a bounded oversized URL
authority parser. M02 terminates malformed source-hanging cases safely; multibyte
charset and platform-native error equivalence are qualified only for the retained
fixtures. These are explicit boundaries/deviations, not silently claimed parity.

No production merge or release.
