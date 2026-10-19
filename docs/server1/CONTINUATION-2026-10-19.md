# Server continuation after execution limit

Task is unfinished. The execution service reported a preview usage limit during legacy media, downloader and archive work. This checkpoint preserves those work-in-progress files; they are not registered into the accepted main build and are NOT asserted complete or reviewed.

Last tested published checkpoint:7022f98db72e5df44f2163783caeb8899aae5fc6 (source tree identical to local1c9f0d2). Fresh Release build independently passed150/150 tests. The native torrent-to-HTTP-to-HLS path was actually exercised through FFprobe/FFmpeg and decoded. Source-only regex backend has no JS runtime. See CLEAN-150-HLS report and individual packet receipts for exact limits.

## Resume

1. Rebuild fresh Release using pinned Qt6.11.1/static libtorrent2.0.11 and correct native assertion ABI. Do not trust recovered incremental object timestamps; a stale double-erase ProcessDriver object was reproduced and diagnosed.
2. Review unregistered M07 LegacyHlsService/LegacyMediaRoutes and N07 DownloadManager/DownloadRoutes against exact pinned oracle. Their partial controlled tests passed but receiving review/integration is incomplete. Legacy source -vbsf argv is rejected by installed FFmpeg7.1.5; preserve source behavior or qualify intended binary, not silently modernize.
3. A00 archive byte/key/multipart primitives and A02 ZIP code are partial work. Exact source tests showed range/length quirks; do not mistake stubs, authored code or unregistered tests for acceptance. Remaining RAR/7z/TAR/GZIP/FTP/NNTP/NZB and other ancillary services still need implementation.
4. Review K12/NATIVE-POLICY-PROPOSAL-20261019. Exact source retains simultaneous raw-alias connections with equal peer IDs, while libtorrent canonical endpoint/peer-ID rules differ. No vendor patch was applied or authorized by this proposal. Investigate smallest hook and obtain scope approval before modifying pinned dependency. Native default handshake timeout can also preempt source timers.
5. Windows bounded output, continuous remote conversion transport, intended Stremio runtime fingerprint, desktop embedding, full raw-server comparison/public-network/player qualification remain open.

Known TLS/oversized/malformed-input safety deviations are explicit in packet receipts; full1:1 parity is not claimed. No production merge or release.

Verified candidate: https://github.com/kingoftheseas56/Colosseum/commit/7022f98db72e5df44f2163783caeb8899aae5fc6
