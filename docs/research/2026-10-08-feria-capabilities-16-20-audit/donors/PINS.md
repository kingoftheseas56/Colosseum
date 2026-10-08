# Compatible donor pins: Feria capabilities 16–20

Source-only resolution performed 2026-10-08 UTC. Receiving Windows target: **Qt 6.11.1**, as assigned. Colosseum baseline: `62057bfbdaa2305a05d2d3fb02b870bc9e826638`. No engine/product build or product test was run.

## Exact audit pins

| Family / role | Exact revision | Authoritative evidence |
| --- | --- | --- |
| QtWebEngine 6.11.1 public embedding layer | `eb0793cc4b76e93cf669f586fd68c76019f40ec9` | [Qt release tag](https://code.qt.io/cgit/qt/qtwebengine.git/tag/?h=v6.11.1), tag object `7aa0d2a34896cfbe88ce0e9ea69bb4e943e1c998`; `git ls-remote` independently agrees. |
| Exact bundled Qt Chromium fork | `58c11ad487f8a237cf0ac71cc3e818b52db150df` | [Parent src tree/gitlink](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9); `.gitmodules` maps `src/3rdparty` to `../qtwebengine-chromium.git`. |
| Standalone Chromium reference matching Qt's declared base | `56ca847e9ea70a5c56fa3d634361da1002fb284b` = `140.0.7339.264` | [Official tag resolution](https://chromium.googlesource.com/chromium/src/+/refs/tags/140.0.7339.264?format=JSON); [pinned chrome/VERSION](https://chromium.googlesource.com/chromium/src/+/56ca847e9ea70a5c56fa3d634361da1002fb284b/chrome/VERSION). |
| CEF embedding comparison, branch 7339 | `49f5d095ac0c84b2da64d06fec866f22531b331d` | [Official Bitbucket commit](https://bitbucket.org/chromiumembedded/cef/commits/49f5d095ac0c84b2da64d06fec866f22531b331d); [pinned compatibility file](https://bitbucket.org/chromiumembedded/cef/src/49f5d095ac0c84b2da64d06fec866f22531b331d/CHROMIUM_BUILD_COMPATIBILITY.txt). |
| CEF's actual Chromium dependency | `754bf56217050dfc97f626926b8dbc6f22b12634` = `140.0.7339.214` | CEF compatibility file explicitly requests `refs/tags/140.0.7339.214`; [official tag resolution](https://chromium.googlesource.com/chromium/src/+/refs/tags/140.0.7339.214?format=JSON). |

## Relationship, including the important version mismatch

The pinned Qt [CHROMIUM_VERSION](https://code.qt.io/cgit/qt/qtwebengine.git/plain/CHROMIUM_VERSION?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9) says:

- Based on Chromium `140.0.7339.264`.
- Security patches through Chromium `148.0.7778.96`.

However, the exact vendor gitlink's [chromium/chrome/VERSION](https://code.qt.io/cgit/qt/qtwebengine-chromium.git/plain/chromium/chrome/VERSION?id=58c11ad487f8a237cf0ac71cc3e818b52db150df) contains `140.0.7339.225`. Its [pinned VERSION history](https://code.qt.io/cgit/qt/qtwebengine-chromium.git/log/chromium/chrome/VERSION?id=58c11ad487f8a237cf0ac71cc3e818b52db150df) and [M140 import log](https://code.qt.io/cgit/qt/qtwebengine-chromium.git/log/?id=58c11ad487f8a237cf0ac71cc3e818b52db150df&qt=grep&q=140.0.7339) show import commit `92193973ffad825f3f86604d8de8b1c15d51a619`, followed by missing-files commit `95f28a5bb7cf7b8df708d288a2dc3acfbe45d667`. Official upstream `.225` resolves to `aa324b3754009b927f7db643b2e837d6a5383b04`.

The parent Qt [January M140 upgrade](https://code.qt.io/cgit/qt/qtwebengine.git/commit/?id=ffcb86d7259a85fbd69907fb105d5684c7809693) already declared `.264`; the latest vendor commit is explicitly a manually cherry-picked security backport. This establishes a Qt-maintained imported/patched lineage, **not a byte-identical upstream Chromium checkout**. The `.225`/`.264` discrepancy is preserved rather than silently reconciled. A full source-tree equivalence comparison was not attempted.

Therefore:

1. Use the exact Qt vendor gitlink for assertions about the Chromium code actually bundled by Qt 6.11.1.
2. Use standalone upstream `.264` as the primary Chromium comparison matching Qt's declared base. Any decisive adaptation/portability finding must also be checked in the relevant exact vendor source; do not silently substitute current main.
3. Security-backport level `148.0.7778.96` does **not** imply Chromium 148 public/internal APIs or feature behavior.
4. The CEF pin is same-major/same-build-family compatible for source comparison, but its supported dependency is `.214`, not Qt's declared `.264` or imported `.225`. There is no exact CEF dependency match in the inspected 7339 history. No Qt/CEF ABI interchangeability, drop-in embedding, or runtime compatibility is established.

### CEF mismatch search boundary

The [pinned 7339 compatibility-file history](https://api.bitbucket.org/2.0/repositories/chromiumembedded/cef/commits/49f5d095ac0c84b2da64d06fec866f22531b331d?path=CHROMIUM_BUILD_COMPATIBILITY.txt&pagelen=50) was inspected from the pinned head back through every M140 entry and the preceding M139 entry. Its M140 updates are `.214`, `.185`, `.41`, `.24`, `.16`, and `.0`; the latest dependency update is `faef09bbdc5ea30a12e7c71592c0f07e7fb6f3b8`. Thus no `.264` or `.225` match exists along that inspected branch lineage. Older pagination is outside the relevant M140 range; no claim is made about every other CEF ref.

## API and build boundary

The receiving target's API authority is pinned QtWebEngine **6.11.1** source and its public C++/QML declarations. Chromium internals and CEF interfaces do not become callable Qt APIs by sharing M140. Individual API/build-feature exclusions must be established by the family audit in their exact files.

Qt `.cmake.conf` says module version `6.11.1` and minimum CMake `3.19` for **building WebEngine itself**; this is not a verified Colosseum application minimum. It also retains an `alpha1` prerelease segment, while the authoritative annotated tag is explicitly “Qt 6.11.1 release”; that field is not used to override tag provenance. No application build or vendor compilation is part of this audit.

## Cloud Qt status

**Not used / unverified.** Cheap discovery found `/usr/bin/qmake` is a `qtchooser` wrapper. `qmake -query QT_VERSION` failed because `/usr/lib/qt5/bin/qmake` is missing; `qtpaths6` and the narrow installed Qt/WebEngine package query returned no result. This does not prove no Qt exists elsewhere. No installed Windows or cloud Qt runtime was inspected, and no product was launched. See [command transcript](metadata/cloud-qt-check.txt).

## Evidence integrity and access limitations

[pins.json](pins.json) records canonical URLs, exact IDs, the full local metadata manifest, byte lengths, SHA-256, Git-style SHA-1 of each saved payload, and failed requests. Hashes for HTML/JSON are evidence-file hashes, not claims that those payloads are source-tree Git objects.

| Saved evidence | SHA-256 |
| --- | --- |
| `cef-compatibility-pinned.txt` | `7092ee659f8421500fc67829ca701e6fe07c45c1d0842731f0851041e61cdfd8` |
| `chromium-cef-tag.json` | `e1085fa73e839f2fee2ff5e8a5edd67812395f3fe11f951a07a20adaf842a746` |
| `chromium-qt-VERSION` | `cbe6f41cc3ba01347fbf11157f2b502acb08f6f5273738f9af8ee9b9b29b10d7` |
| `chromium-tag.json` | `0c94212d91db334026b2be6d8d95733f5f84e757dbc0443d180728936ab7a706` |
| `qt-CHROMIUM_VERSION` | `32b5acc38afc2b8870b01b7770bd0cc6da31fbe9f1878cdbda9fd8f80ac5d5f1` |
| `qt-gitmodules` | `82c41c7f71a71b48ba2278fc29bc092337a173717b395fef7740fff9f0e843bb` |
| `qt-src-tree.html` | `c7fe4edf592e33382153a93aa0f8bb3bab246cf5a158e3b9e52892a0d3124b75` |
| `qt-tag.html` | `9fdae7736d4a54add7b037c13f4e11f9f6b8c8786de38f56fdaa6eb4c2fa900c` |
| `qtchromium-VERSION` | `ce3a513eaf4c8b8574f8512e64b0e51d66cae4ce1649cf17c2638910ae96a485` |

- Authoritative code.qt.io HTML/raw source and Git refs, Chromium Gitiles JSON/raw source, and official CEF Bitbucket API/raw source were reachable with direct HTTPS. The web-open tool initially reported Qt inaccessible; direct source retrieval worked.
- Some Qt vendor tree/commit requests returned HTTP 503, a large import-diff request timed out, and trying the parent branch name `6.11.1` in the separate vendor repo returned 404. No failed request is counted as inspected code.
- Official Qt GitHub mirrors were used only for same-SHA release-tag cross-check and vendor commit metadata after code.qt.io's commit-page failure. The authoritative parent gitlink already established the vendor SHA.
- The current official CEF GitHub README was viewed only to confirm the project/mirror location; all CEF revision and dependency findings use official Bitbucket pinned evidence. Unpinned master is not a capability evidence source.
- No engine build, product tests, runtime qualification, or binary compatibility check was performed. No additional donor families were added and no category winners were selected here.
