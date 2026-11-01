# Nuvio engine source pin (Colosseum Android playback backend candidate)

Public C API headers vendored for the opt-in `COLOSSEUM_ANDROID_NUVIO_BACKEND`
build. No compiled binaries, no embedded Git checkout, no vendor tree: only the
two pinned headers, the upstream license notices, this manifest and the
reviewable hardening patch.

- Upstream project: Nuvio Engine (`nuvio_engine`)
- Donor branch: `codex/nuvio-android-hardening-20261018`
- Pinned upstream baseline: tag `v0.1.5`, commit
  `99a18601fa12858cc8c62576dc72b87edbd81b9a`
- C API version pinned by the headers: `NUVIO_ENGINE_API_VERSION 3`
- Files vendored verbatim from the pinned baseline (before the Colosseum
  hardening patch, which changes torrent delivery, deterministic tests and
  the donor's pinned dependency-patch preparation, never the public C API):
  - `include/nuvio_engine/nuvio_engine.h`
  - `include/nuvio_engine/export.h`
- Licenses: `LICENSE` (GPL-3.0), `THIRD_PARTY_NOTICES.md` — preserved
  verbatim. Internal prototype authorization is NOT shipping or licensing
  approval; dynamic linking does not remove GPL obligations.

## Hardening patch

`patches/0001-android-hardening.patch` — applies to the pristine `v0.1.5`
baseline (`git apply --check` verified in this worktree). It:

1. removes the speculative `serve_piece()` path that streamed
   `block_finished_alert` bytes to HTTP clients before whole-piece hash
   verification (verified `read_piece` / verified-cache delivery preserved);
2. disables LSD, UPnP and NAT-PMP in the Android profile and adds the
   `NUVIO_ENGINE_DISABLE_DISCOVERY` environment seam so synthetic fixtures
   disable discovery and bind their libtorrent listener to loopback only.
   Normal production peer-listening interfaces are unchanged;
3. adds `tests/stream_integrity_regression_test.cpp` with seven loopback-only
   integrity regressions, including an internal test-build-only production
   waiter/writer harness. Its exact unsafe-path mutation must deliver bytes
   before whole-piece verification (red), while restored production code
   must block those bytes and permit verified Range reads/cancellation (green);
4. preserves the upstream order and intent of all twelve pinned
   `cmake/patches/libtorrent-2.0.12*.patch` files. Their Git blobs are already
   LF, but Windows Git checkout with `core.autocrlf=true` can produce CRLF
   patch files. `cmake/Dependencies.cmake` creates LF-normalized *build-tree
   copies* before invoking `git apply`, rather than relying on checkout
   policy. At pinned libtorrent revision
   `740a0b9aeabe00e762cc0efe4a0f27593db2550b`, all twelve must pass
   clean forward application or full reverse-check and post-apply sentinels.
   Actual hunk divergence is fatal; repeat configure is idempotent;
5. limits the Android NDK r27c libc++ experimental C++20 opt-in to the
   `nuvio_engine` CMake target. The pinned Windows NDK has headers for
   `std::jthread` and `std::stop_token`, but suppresses them unless
   `_LIBCPP_ENABLE_EXPERIMENTAL` is set. A task-local API28 ARM64
   compile/link probe with `-Wl,-z,defs` passed before enabling this
   option; it preserves the existing cancellation code. Device/runtime
   behavior remains unqualified;
6. applies the existing `cmake/nuvio-engine-linux.map` version script and
   `--exclude-libs,ALL` to Android shared builds as well. The mapped C API
   remains public, while embedded OpenSSL/Boost/libtorrent symbols stay private.
   The Android linker also uses strict defined-symbol/RELRO binding options;
7. extends the pinned streaming-stall patch so upload-mode cancellation drains
   queued requests after a completed torrent releases its piece picker. Picker
   accounting runs only while the picker exists; sent requests still receive
   wire cancels. `tests/android/nuvio_cancel_requests_regression.cpp` in Colosseum
   exercises both live-picker cancellation and deterministic picker retirement
   on the session thread with a real loopback peer and requests in flight.

The Android preparation verifier accepts equivalent LF/CRLF checkouts,
rejects stray carriage returns and validates the twelve complete patch
declarations in `--verify-only` mode. After a real configure it checks full
reverse application against the populated pinned libtorrent source. Host builds are not Android
ABI, packaging, or runtime acceptance.

## Prepared artifact contract

`scripts/android/prepare_nuvio_engine.py` builds the private
`libnuvio_engine.so` from the patched donor source and stamps the prefix with
`.colosseum-nuvio-hardened-stamp`. The Colosseum CMake feature fails closed
unless both the library and the stamp exist: an unpatched release AAR can
never satisfy the readiness check.
