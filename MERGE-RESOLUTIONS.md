# MERGE-RESOLUTIONS — android/a02-platform-seams ← origin/master

Merge endpoint: `android/a02-platform-seams` (merges into `android/installable` after review).
Rule applied: the branch is 99 commits behind master, so desktop/shared code takes **master**;
the branch keeps **only** its Android seam hooks. Android behaviour lives in
`qml/+android/*`, `native/platform/android/*`, `native/cmake/android.cmake` — never re-inlined
into shared files.

Counts by kind: **took master 4**, **kept hook 4**, **combined 19** (27 conflict blocks in 9 files).
Plus 3 non-conflict **guards added** for master-only desktop features that cannot build on Android.

| file:line | kind | why |
|---|---|---|
| docs/superpowers/specs/2026-09-29-discover-sidebar-design.md:5 | took master | path-clean wording; no seam. |
| native/CMakeLists.txt:932 | combined | master's feria/WebView2 link block + branch's `if(NOT ANDROID)` guard around desktop-only libs (WebEngineQuick/MpvQt/libmpv). |
| native/account/AccountRuntime.cpp:613 | combined | master's delegating ctor chain; branch's factory default store kept. |
| native/account/AccountRuntime.cpp:628 | combined | signature = master's `AccountRuntimeCredentialOptions` seam. |
| native/account/AccountRuntime.cpp:637 | combined | `m_credentialStore(createAccountCredentialStore())` (branch platform seam) + master's `m_stremioCredentials`. |
| native/account/AccountRuntime.cpp:668 | combined | default stremio callbacks (master) using `m_credentialStore->` (branch unique_ptr) + master's `m_stremioCredentials` arg. |
| native/account/AccountRuntime.cpp:695 | combined | `m_sensitiveClipboard(createAccountSensitiveClipboard())` (branch seam) before `m_recoveryKeyPresenter`. |
| native/account/AccountRuntime.cpp:705 | combined | `credentials.accountStore ? ... : m_credentialStore.get()` — master injection, branch store type. |
| native/account/AccountRuntime.cpp:717 | combined | `setCredentialCallbacks(m_stremioCredentials.*)` — master refactor over branch's inline lambdas. |
| native/account/AccountRuntime.h:65 | combined | master's options ctor; dropped branch's now-redundant unique_ptr ctors; kept platform-seam comment. |
| native/account/AccountRuntime.h:153 | combined | `unique_ptr<AccountCredentialStore> m_credentialStore` (branch seam) + `m_stremioCredentials` (master). |
| native/main.cpp:693 | kept hook | branch's platformRuntime facade + shared `catalogNam` + `#if !defined(Q_OS_ANDROID)` guard; master's updater body retained around it. |
| native/main.cpp:718 | kept hook | updater downloader uses shared `catalogNam` (branch seam). |
| native/main.cpp:1537 | kept hook | CatalogVault uses shared `catalogNam` (branch seam). |
| native/main.cpp:1851 | kept hook | `WindowMode` stays inside `#if !defined(Q_OS_ANDROID)` (branch seam). |
| qml/ExtensionsHouseHero.qml:177 | took master | branch's `houseApps` extraction is a non-seam refactor; master's "Tributes" list wins. |
| qml/Main.qml:44 | combined | branch's `televisionMode` alias + master's `feriaReturnSnapshot`. |
| qml/Main.qml:1058 | combined | branch's `worldDefinitions` registry (Android tab source) kept; added master's `Universes` world to it. |
| qml/Main.qml:1131 | combined | `closeWorld()`: branch's `if (win.androidHost) return` + master's `parkFeria(true)`. |
| qml/Main.qml:3591 | took master | hero kicker renamed `UNIVERSE`→`TRIBUTE` by master; shared text wins. |
| qml/Main.qml:4547 | combined | kept branch's ordering comment over master's identical vaultComic `onLoaded` body. |
| qml/Main.qml:5102 | combined | taskbar visible: branch's `!androidHost` + master's feria-fullscreen condition. |
| qml/TopBar.qml:35 | combined | branch's `androidHost` no-op hook + master's new `wallpaperEnabled`. |
| qml/TopBar.qml:228 | took master | master's `leftChrome` id/objectName/anchors layout (shared). |
| qml/TopBar.qml:278 | took master | master's compactChrome-aware Glass `x` (shared). |
| qml/VaultPage.qml:1121 | took master | master restructured the browse face (`vaultStorageGlass`); base file is desktop. |
| qml/VaultPage.qml:2074 | took master | master's Back-control placement; base file is desktop. |

## Non-conflict guards added (master-only desktop features vs the Android build)

The branch is the only side that builds Android (`android.yml`); master never did. master's new
desktop-only Feria (Portico / QtWebEngineQuick) would otherwise enter the Android target graph.
The Android Qt kit installs only `qtwebsockets qtimageformats` (`scripts/android/install_toolchain.sh`),
so WebEngineQuick is unavailable. These are the same desktop-out-of-Android seam the branch already uses:

- `native/CMakeLists.txt` — `add_subdirectory(feria)`, `feria_discovery`, `find_package(unofficial-webview2)`
  and the WebView2 post-build copy moved inside the existing `if(NOT ANDROID)` block.
- `native/main.cpp:150` — `feria/*.h` includes wrapped in `#if !defined(Q_OS_ANDROID)`.
- `native/main.cpp:~2048` — the `PorticoComposition` / `FeriaBrowserPolicy` / `FeriaAccountStore`
  composition block wrapped in `#if !defined(Q_OS_ANDROID)`.

No Android seam was re-inlined into a shared file; the guards only keep desktop-only code off the
Android graph.
