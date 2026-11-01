# Colosseum server M07 source checkpoint

This is a complete relevant **server source/build-input snapshot**, not just a
patch and not the unrelated Colosseum application. It contains public baseline
`f32f84c963cec05e0b0c253ac5829f410dfe7854` plus the 18 newly authored M07 files/edits.
The original tested manifest is preserved unchanged under `evidence/`.
All 356 project source/build-input files have per-file origin records in
`SOURCE-PROVENANCE.json`. A fresh ZIP extraction independently rebuilt the server
and passed 158/158 CTests in 113.12 seconds; its binary SHA256 exactly matched the
original reviewed Linux binary. Raw restore receipts are in `evidence/restore/`.

The reviewed Linux candidate passed **158/158 registered CTests**: all 150 prior
gates plus eight M07 gates. The detailed scope, historical failures and remaining
gaps are in `evidence/DIAGNOSTICS-at-test.md`. Overall 1:1 Stremio server parity is
not established. This checkpoint is not a production release or Windows-qualified
artifact. No GitHub commit or push is represented by this package.

## Contents and exclusions

Included: server C++/C/header sources, CMake, tests and synthetic fixtures, required
peer/oracle helpers, original license notices, raw focused/full test receipts,
the original manifest, and a complete 18-file patch for comparison/restoration.

Excluded: `.git`, unrelated application/addon/FTP work, any unavailable old
unpublished H00/local-addon correction, SDKs/vendor binaries, executables,
libraries, dependency archives, compiler outputs, caches, temporary HOME, copied
credentials, and the upstream Stremio bundle. Published baseline H00 source is
included unchanged because the server depends on it. Small synthetic media and
torrent fixtures are test data, not vendor executables or downloaded content.
The historical Windows dependency lock and one experiment's source include are
retained for source completeness; they are not portable Windows setup or new tests.

**Not a fully offline build:** install/acquire dependencies separately. The
official oracle is fetched only by the explicit acquisition script below. CMake
fetches hash-pinned libuv source. No public torrent/tracker is needed by these tests.

## Linux prerequisites

Qualified environment: Debian 13 x86_64, GCC 14.2, CMake 4.4.3, Ninja 1.13.2,
Qt 6.11.1 Core/Network/Concurrent, Boost 1.83 headers, static libtorrent 2.0.11
**Release** ABI, OpenSSL 3.5.7 development files, zlib, brotli, Python 3,
Node **24** (tested 24.19.0), PowerShell 7.6.7, and system FFmpeg/FFprobe 7.1.5
at `/usr/bin/ffmpeg` and `/usr/bin/ffprobe`.

On Debian, system prerequisites normally come from `build-essential`,
`libssl-dev`, `zlib1g-dev`, `libbrotli-dev`, `python3`, `python3-venv`, and `ffmpeg`.
Use official vendor sources or well-known package registries. Versions and exact
archive URLs/SHA256 for Qt, ICU, Boost, libtorrent, PowerShell and libuv are in
`DEPENDENCIES.json`. CMake/Ninja can be installed in a disposable Python venv with
`python3 -m pip install cmake==4.4.3 ninja==1.13.2`; acquire Node 24 from nodejs.org.

Verify each dependency download against its recorded SHA256 before extraction.
Use a 7-Zip extractor that preserves symbolic links. Extract Qt base directly
into `QT_ROOT` (its archive root contains `bin/`, `lib/`, and `include/`), then
extract ICU directly into `QT_ROOT/lib` (its archive root contains the ICU shared
libraries and symbolic links). Extract Boost headers. Build libtorrent with
`scripts/build-libtorrent.sh`, setting `LT_SOURCE`, `BOOST_ROOT`, and `LT_ROOT`.
This recipe preserves encryption and does not patch libtorrent. Put portable
PowerShell's `pwsh`, CMake, Ninja, Node and Python on PATH. Existing PowerShell
fixtures invoke bare `python`: activate a Python 3 venv or install Debian
`python-is-python3`, so both `python` and `python3` resolve to Python 3. Honor the upstream
license notices for dependencies acquired separately.
The `openssl` executable is also needed by an existing temporary local TLS test.

## Restore, verify, build, test

1. Extract this ZIP to a fresh writable directory. Run
   `python3 scripts/verify-package.py --restore-executable-bits` before adding
   generated build files. This restores executable fixture bits if the ZIP
   extractor discarded them; it never changes source bytes.
2. Run `python3 scripts/acquire-oracle.py`. This obtains the official
   `https://dl.strem.io/server/v4.21.1/desktop/server.js`, verifies its 6,676,503
   bytes and SHA256
   `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`, and
   regenerates the module index. It does not start the original server.
3. Set `QT_ROOT`, `LT_ROOT`, `BOOST_ROOT` to the dependency prefixes and optionally
   `NODE_BIN` to Node 24. Node 24 must also be on PATH because some existing
   helpers invoke bare `node`. Run `bash scripts/build-test.sh`.

The script configures this package's `native/colosseum_server_v1`, not the original
workspace. It uses fresh build/cache directories and the fixture-only interface
mode; production discovery defaults are unchanged. Do not run two full suites
concurrently because some legacy wire tests use fixed loopback ports.

The historical K10 failure-drain gate has shown timing sensitivity. Keep any raw
failure and rerun it in isolation rather than silently changing expectations.
Legacy `-vbsf` FFmpeg arguments remain source-compatible even where modern FFmpeg
rejects them. Real ffsplit execution and Windows remain unqualified.

## Known M07 boundaries

Preparation coalescing and the pinned `waitForSubs` module-scope defect remain
open. The candidate rejects retained ordinary MP4 metadata atoms above 256 MiB;
that explicit resource ceiling is a documented difference from unbounded upstream
behavior. No comprehensive malformed-input or whole-server parity claim is made.

Current source delivery was prepared for the user's explicit request after an
inspection of these actual bytes. Historical records are retained unchanged;
they are not rewritten into a claim that an earlier unavailable payload was
recovered or approved.
