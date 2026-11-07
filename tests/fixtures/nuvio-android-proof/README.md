# Android Nuvio proof repository

This is a deliberately small proof plugin, not a claim of general third-party
Nuvio provider compatibility. CI adds the manifest and enables the script by
fetching them through the normal repository store over HTTPS, pinned to the
exact Git commit under test. It does not inject a successful resolver response.

The plugin requests live Internet Archive metadata and returns a direct HTTPS
URL only when the original public-domain item, public-domain rights mark, and
specified MP4 are present. It supports only Night of the Living Dead (1968),
TMDB 10331 / IMDb tt0063350. It checks that JavaScript sees only the existing
TMDB placeholders; it needs no key and does not change native authorization.

- [Library of Congress public-domain explanation](https://blogs.loc.gov/copyright/2020/10/copyright-horror-stories/)
- [Original B&W video metadata](https://archive.org/metadata/Night_Of_The_Living_Dead_raw_HD_WS)
- [Direct MP4](https://archive.org/download/Night_Of_The_Living_Dead_raw_HD_WS/NightOfTheLivingDeadRawUnrestored.mp4)

Source check on 2026-11-07: metadata returned a public-domain mark and the raw
untouched original. A 1,024-byte range GET returned HTTP 206, video/mp4, and an
MP4 `ftypisom` header. This is separate from Android runtime proof, which must
still pass in the emulator. No full movie download is performed by the proof.

The native crash and deadline stages use separate, explicitly synthetic local
fault scripts, not this network plugin. Their results cannot satisfy the live
repository/direct-stream stage. A separately disclosed Stremio protocol fixture is fetched over exact-commit
HTTPS through the existing Extensions registry and unchanged AddonClient. It
contains the independently range-verified public-domain MP4. No in-memory
Stremio success callback is injected. The proof-only overlay selects that
fixture without changing the default providers' registry state. The actual
source picker must keep its nonempty Stremio rows and the complete relevant
registry fingerprint intact through both service failures, then repeat both
real Nuvio and Stremio queries before reporting success.

This proves Stremio protocol coexistence and recovery, not Torrentio/NoTorrent
uptime or broad community-plugin compatibility. Both default provider endpoints
returned HTTP 403 from the development cloud during prechecks. Their network
availability is not silently counted as passing.

Run deterministic checks with:

    node --test tests/fixtures/nuvio-android-proof/test-plugin.mjs

Optional bounded live metadata and 1 KiB MP4 check:

    node tests/fixtures/nuvio-android-proof/test-plugin.mjs --live
