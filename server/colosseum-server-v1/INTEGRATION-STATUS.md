# Standalone server source checkpoint: activation prohibited

Receiving decision, 2026-10-21: accepted for source preservation and continued
standalone development. This is not acceptance of whole-server Stremio parity,
Windows runtime qualification, Android qualification, or application integration.

The user's activation condition is evidence that this server does everything
the existing `server.js` does. Until that condition is independently accepted,
do not connect this project to Colosseum's application build, startup, streaming
selection, players, runtime fallback, installers, APKs or release packaging.
There is no automatic switch to this implementation.

## Source identity and layout

This directory contains the complete 396-file delivered packet, unchanged, plus
this receiving note and directory-scoped Git attributes/ignore rules. Original
payload identities remain verifiable through `PACKAGE-MANIFEST.json` and
`scripts/verify-package.py`. The manifest does not describe these added landing
files and excludes itself as documented by its author.

- ZIP: `colosseum-server-m07-source.zip`, 1,075,652 bytes.
- ZIP SHA256: `a6c04442b70728d31370fae0d73597a878b15bd10275aefe224e40c006aa7b06`.
- Manifest SHA256: `c8efee41740dae2f8b860910c3234dd65724f86133714bc14ac5cdfe2879042d`.
- Source baseline: `f32f84c963cec05e0b0c253ac5829f410dfe7854` from
  `feature/colosseum-server-1.0`, plus the 18 delivered M07 edits/additions.
- CMake source directory: `native/colosseum_server_v1` relative to this directory.
  Build it explicitly; the application's CMake project does not include it.
- Oracle: official desktop CDN selector v4.21.1, embedded package v4.21.0,
  SHA256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
  No upstream bundle, dependency libraries or server executable is committed here.

## Evidence accepted and its limits

Receiving review independently verified the archive and all 395 manifest-listed
file hashes, and compared 338 unchanged project inputs against actual public
baseline Git blobs. All matched. The remaining 18 project inputs are the
delivered M07 changes; their exact tested hashes also matched.

Source review covered the changed legacy route/composition, HLS rendering and
MP4/MKV indexing implementations and their tests. The packet retains raw Linux
fresh-restoration evidence: 236 build steps and 158/158 registered CTests passed
in 113.12 seconds. Its reported restored binary matches the reported original
Linux binary. These are reviewed supplier receipts, not a new receiving Linux
build or a proof of every public API behavior.

Receiving review freshly downloaded and verified the official oracle, extracted
1,312 modules without starting its server entry point, and ran the controlled
renderer reference harness. All 286 generated cases matched the delivered
renderer fixture structurally. This validates reference generation; it does not
constitute a fresh comparison against an executed C++ candidate.

A receiving Windows CMake configure reached the required zlib dependency and
failed because `ZLIB_LIBRARY` and `ZLIB_INCLUDE_DIR` were unavailable. No receiving
native build or CTest run is claimed. Several M07 process/media gates are also
registered only on UNIX; Windows needs equivalent runtime coverage before it
can be accepted. No devices, live public torrents, application runtime or
packaging were exercised in this receiving review.

## Open parity and activation gates

1. Complete keyed preparation coalescing, including cancellation of one consumer
   while another remains active.
2. Resolve the pinned `waitForSubs` module-scope defect with an explicit behavior
   decision and differential tests; do not silently describe a deviation as parity.
3. Qualify actual `ffsplit`, remote conversion and deployed FFmpeg arguments.
   Legacy `-vbsf` compatibility is not established on modern FFmpeg.
4. Keep the 256 MiB retained MP4 metadata ceiling documented as a deliberate
   difference from the oracle's unbounded behavior. It remains a parity
   exception requiring the owner's explicit acceptance before activation.
5. Build an endpoint/behavior inventory for the entire pinned desktop server,
   then close every relevant family with independent differential evidence:
   torrent/session ownership and discovery, HTTP/Range and seeks, proxying,
   indexing/HLS/transcoding, subtitles, settings, errors and process lifecycle.
   Passing a bounded M07 suite does not close untested server families.
6. Verify malformed-input/resource behavior and shared-operation shutdown, then
   qualify Windows and any intended Android ABI/JNI/runtime independently.
7. Before distribution, qualify exact dependencies, licenses/notices, executable
   provenance and packaging. Preserved baseline notices are not a new binary
   compliance certification or a conclusion about the licensing of translations.
8. Only after the owner accepts whole-server evidence and any explicit deviations
   may a separate change propose application wiring. This commit grants no such
   authority.

Historical diagnostics and failed baseline runs remain intact in `evidence/`.
The receiving landing does not claim recovery of the unavailable historical
unpublished H00/local-addon corrections, or enable that separate addon path.

The full import whitespace check reports existing whitespace in raw build logs,
the original patch, vendored code and an unchanged baseline test. These bytes
were deliberately preserved to retain packet hashes; the warning is not claimed
as a passing check or corrected by rewriting historical evidence.
