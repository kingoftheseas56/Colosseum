# Pinned oracle runtime alignment

The historical runtime authority remains unchanged:
`REFERENCE-RUNTIME.json` identifies portable Node v22.16.0;
`RUNTIME-SURFACE-MATRIX.json` requires v22.16.0 for its runtime observations;
`EVIDENCE-LOCK.json` records that exact version. This portable Node is not the
unavailable intended stremio-runtime companion identity. Production native code
does not require Node.

Configure oracle tests with `-DSERVER1_NODE_EXECUTABLE=/path/to/node-v22.16.0/bin/node`.
The official Linux x64 archive is available at
https://nodejs.org/dist/v22.16.0/node-v22.16.0-linux-x64.tar.xz;
SHA256 `f4cb75bb036f0d0eddf6b79d9596df1aaab9ddccd6a20bf489be5abe9467e84e`,
verified against the release SHASUMS256.txt.

## Statistics backend

Node v22.16.0 reports libuv 1.49.2. Native statistics now pins upstream libuv 1.49.2
from https://dist.libuv.org/dist/v1.49.2/libuv-v1.49.2.tar.gz,
SHA256 `8c10706bd2cf129045c42b94799a92df9aaa75d05f07e99cf083507239bae5a8`.
Its detached release signature verified against maintainer Santiago Gimeno's key,
fingerprint `612F0EAD9401622379DF4402F28C3C8DA33C03BE`, independently identified
in the official v1.49.2 MAINTAINERS.md. The signature verification does not rely on
a locally assigned trust level.

CMake rejects a FetchContent source override with a different libuv version.
The statistics differential retains its strict candidate/oracle libuv-version
check, exact deterministic fixtures and bracketed live statistics assertions.
No tolerances or expectations were weakened. Three focused native/statistics
tests passed against Node22.16.0. Node24.22.1's libuv1.52.1 correctly fails the
version guard against this backend; that profile is not accepted interchangeably.

## Guess-file-index oracle

The runner now defaults to exact v22.16.0. Its existing corpus remains unchanged:
21,080 exact cases passed (8,455 metadata and 12,625 selection). An explicit
`--runtime-profile diagnostic-node24` accepts only v24.22.1 and passed the same
corpus separately. That option is a diagnostic, not authority to change other
runtime gates. The old `--allow-portable-node22` flag remains a compatible alias
for the default pinned profile, and cannot override the exact version check.

## Preserved earlier results

The preceding 197-test checkpoint used Node24.22.1/libuv1.52.1. A separate
Node22.16.0 rerun passed 33/36 original oracle/differential commands: regex had
three semantic differences; statistics rejected the backend mismatch; and
file-index rejected the runtime before its explicit portable flag was supplied.
The separate portable file-index rerun passed all 21,080 cases. Those original
receipts are preserved and are not relabeled as pinned-profile passes.

Regex runtime alignment and the final integrated suite are separate work;
these focused results alone do not establish full server.js parity.
