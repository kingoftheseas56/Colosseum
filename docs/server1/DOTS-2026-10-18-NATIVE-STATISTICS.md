# Native statistics inputs for K12 (partial)

Parent e5c49dadf79c1bd8d0b010e9f6fa293452a2c7f9.
This qualifies measured transport inputs. M172 statistics serialization and its
M416 speedometer are still separate unfinished gates.

## Production changes

- Existing downloadedBytes remains accepted-owned payload accounting. New
  wirePayloadBytesReceived counts piece payload observed on a current connection
  before ownership matching, including duplicate, canceled and unsolicited data.
  Stale connection identities are rejected before this counter changes.
- PeerTransferStatistics snapshots identify current connections and attachment
  order. Native rates are sampled on the existing libtorrent network tick, even
  when no interest/choke transition occurs. No new thread or timer was added.
- Aggregate rates sum the same published per-peer snapshot instead of overwriting
  the entire swarm with the last observed peer. Native uploaded totals retain
  observations from retired connections. Native measurements remain separate
  from the existing explicit upload-framing counter.
- Initial, periodic and retiring samples use the four native stat fields also
  exposed by get_peer_info, without copying every peer's complete bitfield.
  A sampling batch publishes once under the existing mutex after identity checks.
- A successfully framed request re-arms the existing drain for remaining queued
  requests. An in-flight owned request is skipped rather than misidentified as
  autonomous native queue activity. One peer need not finish before another
  peer's queued request can be serviced.
- New TransportStatistics fields are appended, preserving the original aggregate
  initializer field order. Existing contract assertions remain unchanged.

## Real localhost protocol evidence

The actual production adapter/PeerPlugin connects to controlled Python peers.
No public swarms or external media are involved.

1. Normal original plus duplicate: 32768 wire bytes, 16384 accepted-owned bytes,
   one normal completion.
2. Cancel after framing, before local payload release: 32768 wire bytes, zero
   accepted-owned bytes, zero normal completions. This does not claim the peer
   saw the CANCEL frame before sending its held payload.
3. Unsolicited piece: 16384 observed wire bytes, zero accepted-owned bytes and
   zero framed requests. The fixture records both its unsolicited schedule and
   the actual piece send; the native counter, not the send log alone, is checked.
4. Two peers: exact 16384-byte and 73-byte fixture completions; both measured
   native rates positive; aggregate equals their same-snapshot sum. Peer A then
   disconnects through the owned fixture process; active samples retire without
   rolling back cumulative bytes, the remaining rate decays without state-change
   notifications, and close clears live rates.

The new two-peer test reproduced aggregate=14 versus peer sum=3290 before the
sum repair. Afterward aggregate=3290 and peer sum=3290. A repeated full run then
exposed pending-batch starvation; that original failure is retained. After the
existing-drain repair, both new CTests passed five repeats each. The unsolicited
case was added subsequently; the final ordinary and sanitizer runs include it.

The seven existing upload subcounters keep their meanings. Multi-peer native
uploaded-total retention is not independently established by these download
fixtures; no such claim is made here.

## Build/test and retained failures

    cmake --build build/server1 -j2
    SERVER1_TEST_FIXED_INTERFACE=1 ctest --test-dir build/server1 -R 'K12-.*statistics-real-wire' --output-on-failure
    ctest --test-dir build/server1 --output-on-failure --timeout 90

The full suite uses the previously documented explicit fixed-interface test
profile and portable PowerShell setup. Final results and exact executable hashes
are in evidence/dots-2026-10-18/native-statistics-tests.txt.

Retained setup failures:
- Initial two-peer harness requested an 8192-byte non-final native block; corrected
  to the actual 73-byte last piece. This failure is not a product defect claim.
- Held sends use the existing fixture's LATE_PIECE marker even without cancellation.
  The runner now checks the genuine marker with exact request spans and native
  payload identity instead of expecting a different log format.
- Full compilation exposed new fields inserted into an existing aggregate layout;
  appending them fixed it without changing the contract test.
- Debug-ASan live transport failed. That build selected TORRENT_USE_ASSERTS while
  importing the Release-only static libtorrent package. It is an unqualified
  mixed configuration; the precise cause of every failed observation is not
  independently isolated. A separate RelWithDebInfo-ASan build matches the
  dependency's Release interface and passes the actual wire tests. No test or
  assertion was disabled to create a pass. detect_leaks=0 retains the prior
  explicit LeakSanitizer limitation. Imported libtorrent itself is not ASan-built.

## Limits

Native smoothing is not the source JavaScript speedometer. Source queued/unique/
tries/connections semantics, wire JSON fields, selection property presence,
source-visible retirement and full route/streaming qualification remain open.
Windows is NOT RUN. No full Server 1.0 parity or release readiness claim.
