# Windows prebuilt engine assessment

Inspected 2026-10-14. This is a publisher/source and package-metadata search,
not acceptance of another binary. The Conda package was downloaded and its exact
metadata/configuration inspected; no new engine was installed or run.
The target remains QtWebEngine with the secure streaming arc's full gates.

| Candidate | Evidence | Decision / missing proof |
| --- | --- | --- |
| Existing GoldenDict-ng codec overlay | Local overlay manifest identifies `Goldendict-ng-260529-win64-qt6111.7z`, archive SHA256 `e833a49ccbf66679070932a981f1a6ddd22e4724a554f4290ea67671a0399897`; H.264/AAC decoding was observed, engine API reports 6.11.2 on Qt 6.11.1 | Keep as measured codec baseline. Full feature configuration and exact publisher source provenance remain unresolved; its stale-frame permission regression fails. |
| [Conda-forge qt6-webengine](https://api.anaconda.org/package/conda-forge/qt6-webengine) | Downloaded Windows x64 asset `qt6-webengine-6.11.2-pl5321h80dc20f_1.conda`, SHA256 `e047f59a3527359013dd5bd5ccc8ffb75c678ce1167d53a084fd29c628781314`, 189584408 bytes, uploaded 2026-10-03. Requires matching `qt6-main 6.11.2.*` and additional Conda dependencies | Rejected as a complete replacement: packaged generated headers explicitly disable proprietary codecs AND geolocation. PDF/printing, spellchecker and WebRTC are enabled. Quick/Core/helper/plugin/import libraries exist in archive inventory. No runtime qualification performed. |
| [Doug Massay / Sigil Qt builds](https://github.com/dougmassay/win-qtwebkit-5.212/releases/tag/v5.212-1) | Downloaded `Qt6.10.2ci_x64_VS2026a.7z`, 139145832 bytes; publisher SHA256 `a25d85fd192711653d20a5665c45cfa4b118261b09e699c6917ce5a99149d154` matches. Packaged headers enable proprietary codecs, WebRTC, PDF/printing and spellcheck; geolocation is disabled | Rejected as a complete replacement: no WebEngineQuick DLL/import library/CMake package/QML plugin. Core/Widgets SDK exists, but no generated Chromium headers/buildflags or exact source pin. Qt 6.10.2/MSVC 19.50.35723 metadata does not establish compatibility with Feria. No binary executed. |
| [Calibre / bypy](https://github.com/kovidgoyal/bypy/blob/master/bypy/pkgs/qt_webengine.py) | Recipe explicitly enables proprietary codecs and Widgets, disables WebEngineQuick | Potential Core donor only. Matching Quick would still need compilation and private ABI/source verification; no packaged artifact was accepted. |
| [qutebrowser Windows releases](https://github.com/qutebrowser/qutebrowser/blob/main/doc/install.asciidoc) | Publisher documents codec-free Windows builds | Does not solve our proprietary-codec requirement. |

Conda recipe pin inspected:
[`d6573704181dbbe592441715e09bd96cf6b9ae38`](https://github.com/conda-forge/qt-webengine-feedstock/tree/d6573704181dbbe592441715e09bd96cf6b9ae38/recipe).
Publisher recipe flags are weaker evidence than the exact package's feature
headers, source manifest and runtime checks; do not promote them to acceptance.
Conda receiving inspection independently verified the full archive hash and 95
extracted metadata/configuration file hashes. Packaged provenance identifies
build run `github_37074066490`, the feedstock commit above, official Qt 6.11.2
source and VS2022 compiler variant. Receipt directory:
`feria-conda-engine-inspection-20261014` (`result.json`, `selected-hashes.json`,
`archive-inventory.json`, `config-header-snippets.txt`). No DLL/EXE/LIB was
extracted or executed. Generated headers establish packaged configuration,
not independent proof that every binary corresponds to those headers.
Sigil receiving inspection verified the publisher archive digest and all 93
selected metadata/configuration hashes. Its full inventory distinguishes general
Qt Quick modules (present) from WebEngineQuick (absent). Evidence:
`sigil-inspection-20261014`, including `archive-inventory.json`,
`feature-summary.json`, `selected-hashes.json` and publisher asset metadata.
The current GoldenDict ordinary release workflow uses official Qt packages; its
vcpkg dependency export is not a Qt SDK. The exact older codec archive's public
release/recipe was not recovered in this bounded search.

## Security and integration gate

Qt permission-handle source at
[`v6.11.2`, commit a33fa2a897e5ee58e385b3f88dc247d99fca56db](https://github.com/qt/qtwebengine/blob/a33fa2a897e5ee58e385b3f88dc247d99fca56db/src/core/api/qwebenginepermission.cpp)
and
[`dev`, commit a0150f4d48a79483b4ab67f24f63a4f6f9417329](https://github.com/qt/qtwebengine/blob/a0150f4d48a79483b4ab67f24f63a4f6f9417329/src/core/api/qwebenginepermission.cpp)
still validates profile/type/origin without checking the requesting document's
lifetime. This source finding is not a runtime test of either newer build, nor
proof that every third-party patchset lacks a remedy. No searched package has
evidence closing Feria's reproduced stale-frame consent defect.

Next: inspect exact package/source/feature metadata of any proposed replacement,
require matching Core/Quick/helper/resources and the required browser features,
then run the existing positive and stale-frame regressions in a disposable lane.
Prebuilt codecs alone do not establish sandboxing, DRM/CDM availability, provider
acceptance, popup independence or document-bound consent. The unbuilt Core patch
and a full-engine build route remain open alternatives; no release is authorized
by this assessment.

## Bounded Quick-only alternative

Static inspection of the pinned stock Chromium interfaces found a plausible
alternative: Quick receives the trusted requesting frame token and could capture
the native document token before emitting an application request. A single-use
native envelope could reject stale answers synchronously without changing Core's
layout. This is source-level feasibility only: no envelope has been implemented,
compiled, linked or runtime-qualified.

The typed probe cannot yet compile legitimately: matching GN/Mojo generated
headers and Core build configuration are missing. Its real virtual interfaces
must match each actual Core; stock 6.11.1 evidence cannot establish compatibility
with the codec Core reporting 6.11.2. No fabricated headers or vtable declarations
are acceptable. Guarded requests must be exclusive, bound to frame/document,
view/adapter/profile and owner thread, and safe under callback destruction.
BFCache cancellation, concurrent frames, live consent, stale denial and teardown
remain required checks. The envelope would not by itself repair Core's pending
callback retirement or general permission API semantics.

The new stock replacement regression confirms why document identity matters:
both identical-URL reload and identical-URL iframe replacement admitted a stored
grant from the old prompt. QtTest recorded 2 passed, 2 failed, native exit 2;
source, loaded module and helper hashes matched, and sampled children exited.
Evidence: `feria-frame-permission-20261014-stock-replacement-red`.
The failure is at the intended stale-grant assertion, not fixture loading.
