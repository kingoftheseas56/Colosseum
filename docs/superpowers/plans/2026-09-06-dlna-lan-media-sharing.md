# DLNA LAN Media Sharing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one OFF-by-default `Share Colosseum over DLNA` setting that exports Colosseum's canonical playable Vault + completed Downloads library to the local LAN, plus native Android Colosseum discovery/browse/playback of nearby Colosseum servers.

**Architecture:** Keep `VaultIndex` as the sole media truth. Add a small `native/dlna/` projection and UPnP AV layer around pinned libupnp 22.0.6. Desktop is the V1 MediaServer. Android is the first-party control point/client and uses a constrained loopback relay so Media3 can play LAN HTTP without weakening Android's existing global cleartext policy.

**Tech Stack:** Qt 6/C++20, QML, pupnp/libupnp 22.0.6, libupnp IXML/webserver APIs, Android NDK + AndroidX Media3 1.11.0, Java Wi-Fi multicast lock, CMake/CTest, Python contract tests where Android runtime tests are impractical in CI.

**Spec:** `docs/superpowers/specs/2026-09-06-dlna-lan-media-sharing-design.md`

## Global Constraints

- Product surface stays tiny: one desktop sharing toggle and one conditional Android `Nearby Colosseum` source. No additional preference maze.
- OFF means no MediaServer registration and no media HTTP endpoint.
- LAN only. Never create router port mappings, never deliberately bind a public/WAN address, and never expose raw filesystem paths in URLs.
- `VaultIndex` is the only catalogue truth. `VaultDownloadsRoot` already injects completed Downloads into it; do not add a DLNA scanner or database.
- V1 is direct-play only. No transcoding/remuxing.
- V1 exports canonical video rows only. Do not advertise EPUB/PDF/CBZ just because Vault indexes them.
- Android consumes nearby servers but does not persist remote rows into local Vault.
- Do not relax Android's global cleartext network policy. LAN HTTP playback must go through the constrained loopback relay.
- Use current repository truth at execution time. The SHAs in this plan record the inspected design baseline, not permission to implement on stale branches.

## Code Intake Manifest: Exactly What We Are Taking From Where

### Actual external code included in the build

**pupnp/libupnp 22.0.6** is the only DLNA protocol implementation we adopt.

- Upstream: `pupnp/pupnp`
- Tag: `release-22.0.6`
- Commit: `837c6e4401de68deca1aef6254475326a8b87b2a`
- License source: upstream `COPYING` (BSD-style three-clause license)
- Integration: CMake `FetchContent`, exact commit pin, static library target `UPNP::Static`
- We compile/use its upstream `ixml` and `upnp` libraries as a dependency. We do **not** paste selected implementation `.c` files into Colosseum-owned source.

Public libupnp API used by Colosseum:

```cpp
// SDK/device lifecycle
UpnpInit2(...);
UpnpRegisterRootDevice2(...);
UpnpSendAdvertisement(...);
UpnpUnRegisterRootDevice(...);
UpnpFinish();

// Integrated HTTP virtual directory
UpnpAddVirtualDir(...);
UpnpRemoveVirtualDir(...);
UpnpVirtualDir_set_GetInfoCallback(...);
UpnpVirtualDir_set_OpenCallback(...);
UpnpVirtualDir_set_ReadCallback(...);
UpnpVirtualDir_set_WriteCallback(...); // register rejecting/no-write callback only if required
UpnpVirtualDir_set_SeekCallback(...);
UpnpVirtualDir_set_CloseCallback(...);

// Device action callbacks
UpnpActionRequest_get_ActionName(...);
UpnpActionRequest_get_ActionRequest(...);
UpnpActionRequest_set_ErrCode(...);
UpnpActionRequest_set_ActionResult(...);
UpnpMakeActionResponse(...);
UpnpAddToActionResponse(...);

// Control-point path used by Android
UpnpRegisterClient(...);
UpnpSearchAsync(...);
UpnpSendActionAsync(...);
UpnpUnRegisterClient(...);
```

The implementation agent must compile against the pinned headers and use their exact signatures. If an API name has changed at the pinned commit, update only the wrapper call to the exact pinned public API; do not switch versions.

**PThreads4W on Windows** is a build/runtime dependency required by libupnp's MSVC CMake path, not a source of Colosseum DLNA behavior. Supply it through the repository's existing vcpkg workflow. Preserve the exact vcpkg port's installed copyright/license in the distribution inventory. Do not enable libupnp's `DOWNLOAD_AND_BUILD_DEPS` path.

### Existing Colosseum code reused directly

- `native/engine/VaultIndex.*`: canonical media IDs, paths, titles, groups, size, duration, revision signal.
- `native/engine/VaultDownloadsRoot.*`: already projects completed Downloads into Vault truth.
- `qml/ContentPreferences.qml`: persisted preference store pattern.
- `qml/SettingsPage.qml`: current house toggle UI pattern.
- `native/main.cpp`: composition root; receives the existing `VaultIndex*` and wires new DLNA owners.
- Android `Media3PlayerHost` / `AndroidMedia3Item`: existing playback owner. Feed it the relay URL; do not fork a second player.
- Existing QTcpServer/QTcpSocket patterns may be consulted for the Android relay, but the relay gets its own narrow implementation and security contract.

### Reference only: zero copied source

No source is copied from Gerbera, ReadyMedia/MiniDLNA, Jellyfin DLNA, Universal Media Server, Kodi/Platinum, Rygel/GUPnP, or jUPnP/Cling. Their licenses and architectures make them behavioral references only. If an implementation agent is tempted to paste a function, XML builder, quirk table, or service handler from one of those projects, stop and implement the behavior independently from the UPnP/DLNA protocol contract.

---

## Task 1: Establish a Fresh Implementation Baseline

**Files:**
- No source edits until the integration branch is clean and baseline tests are recorded.

**Interfaces:**
- Desktop inspected baseline: `master` at `88f8dd5cc432d67c6712f8fa2ac751227e852dc6`.
- Android inspected baseline: `recon/android-master-2026-09-05` at `174f4c4754a0c05334cec3a667855dd997409d4c`.
- At execution time use the newest available successors, not these SHAs blindly.

- [ ] **Step 1: Boot repository authority before touching source**

Read local Preflight authority in this order when available:

```text
C:\Users\Suprabha\Desktop\Preflight-Architect\AGENTS.md
C:\Users\Suprabha\Desktop\Preflight-Architect\MEMORY.md
C:\Users\Suprabha\Desktop\Preflight-Architect\CAPABILITIES.md
```

Then inspect current branch/worktree status. Do not reuse a dirty shared worktree for implementation.

- [ ] **Step 2: Fetch all relevant refs and create an isolated worktree**

```bash
git fetch origin --prune
git rev-parse origin/master
git rev-parse origin/recon/android-master-2026-09-05
git worktree add .worktrees/dlna-lan-media-sharing -b feature/dlna-lan-media-sharing origin/master
cd .worktrees/dlna-lan-media-sharing
```

- [ ] **Step 3: Reconcile the current Android integration branch before DLNA edits**

```bash
git merge --no-ff origin/recon/android-master-2026-09-05
```

If that branch has a clearly newer successor, merge the newer reconciled Android branch instead and record its SHA in the implementation recap.

Resolve conflicts by preserving current master behavior plus Android platform guards. Do not drop either side merely to make the merge compile.

- [ ] **Step 4: Prove baseline before feature edits**

Run the repository's normal configure/build plus the currently registered desktop tests and Android contract tests relevant to Media3/platform composition. At minimum:

```bash
python tests/test_android_media3_chapter_bridge.py
python tests/test_android_media3_lifecycle_composition.py
python tests/test_android_media3_snapshot_transport.py
python tests/test_android_media3_track_selection.py
```

Then run the appropriate CMake/CTest preset already used by the repository/CI for the current host. Record any pre-existing failures before making DLNA changes.

- [ ] **Step 5: Commit only the integration merge if it produced a merge commit**

```bash
git status --short
git log -1 --oneline
```

No feature commit is allowed while unrelated dirty files remain.

---

## Task 2: Pin libupnp 22.0.6 and Its License Boundary

**Files:**
- Create: `native/cmake/ColosseumPupnp.cmake`
- Modify: `native/CMakeLists.txt`
- Modify: `.github/workflows/desktop-ci.yml`
- Modify: `.github/workflows/release-installer-smoke.yml`
- Modify: `THIRD_PARTY_NOTICES.md`
- Create: `licenses/libupnp-COPYING.txt`
- Create: `tests/test_dlna_dependency_pin.py`

**Interfaces:**
- `colosseum_configure_pupnp()` creates/validates target `UPNP::Static`.
- Exact upstream commit pin is a tested repository contract.

- [ ] **Step 1: Write the failing dependency contract test**

Create `tests/test_dlna_dependency_pin.py` that reads the repository files and asserts:

```python
PIN = "837c6e4401de68deca1aef6254475326a8b87b2a"

assert PIN in cmake_text
assert "UPNP_BUILD_SAMPLES OFF" in cmake_text
assert "UPNP_BUILD_SHARED OFF" in cmake_text
assert "UPNP_BUILD_STATIC ON" in cmake_text
assert "UPNP_ENABLE_TESTING OFF" in cmake_text
assert "UPNP_ENABLE_TESTING_INTEGRATION OFF" in cmake_text
assert "UPNP_ENABLE_SSDP ON" in cmake_text
assert "UPNP_ENABLE_SOAP ON" in cmake_text
assert "UPNP_ENABLE_WEBSERVER ON" in cmake_text
assert "DOWNLOAD_AND_BUILD_DEPS OFF" in cmake_text
assert "UPNP::Static" in native_cmake
assert "pupnp/libupnp 22.0.6" in notices
assert "Intel Corporation" in copied_license
```

Run:

```bash
python tests/test_dlna_dependency_pin.py
```

Expected: FAIL because none of the integration exists yet.

- [ ] **Step 2: Add the pinned CMake dependency wrapper**

Create `native/cmake/ColosseumPupnp.cmake`:

```cmake
include_guard(GLOBAL)
include(FetchContent)

function(colosseum_configure_pupnp)
    set(UPNP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(UPNP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(UPNP_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_TESTING_INTEGRATION OFF CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_CLIENT_API ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_DEVICE_API ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_GENA ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_HELPER_API_TOOLS ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_IPV6 OFF CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_OPEN_SSL OFF CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_OPTIONAL_SSDP_HEADERS ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_SOAP ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_SSDP ON CACHE BOOL "" FORCE)
    set(UPNP_ENABLE_WEBSERVER ON CACHE BOOL "" FORCE)
    set(DOWNLOAD_AND_BUILD_DEPS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        pupnp
        GIT_REPOSITORY https://github.com/pupnp/pupnp.git
        GIT_TAG 837c6e4401de68deca1aef6254475326a8b87b2a
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(pupnp)

    if(NOT TARGET UPNP::Static)
        message(FATAL_ERROR "Pinned pupnp did not provide UPNP::Static")
    endif()
endfunction()
```

In `native/CMakeLists.txt`, include the wrapper after the existing Qt dependency discovery and before `colosseum` is linked:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ColosseumPupnp.cmake)
colosseum_configure_pupnp()
```

Link:

```cmake
target_link_libraries(colosseum PRIVATE UPNP::Static)
```

- [ ] **Step 3: Supply PThreads4W on Windows through existing vcpkg CI**

In the existing Windows vcpkg install command(s), add:

```text
pthreads:x64-windows
```

Do not replace the repository's existing vcpkg baseline/tag. The goal is to satisfy libupnp's `find_package(PTHREADS4W CONFIG REQUIRED)` on MSVC using the same package manager already used for libtorrent/Boost/OpenSSL.

After installation, inspect the exact file at:

```text
<vcpkg-installed>/x64-windows/share/pthreads/copyright
```

and add the exact license identity to `THIRD_PARTY_NOTICES.md`. Do not guess from a different pthreads4w mirror.

- [ ] **Step 4: Preserve libupnp's exact upstream license**

Copy the pinned commit's `COPYING` byte-for-byte into:

`licenses/libupnp-COPYING.txt`

Add a `THIRD_PARTY_NOTICES.md` row describing:

```text
pupnp/libupnp 22.0.6 @ 837c6e4401de... | BSD-style 3-clause terms in licenses/libupnp-COPYING.txt. Used as the UPnP/DLNA protocol library; Colosseum-owned MediaServer code remains MIT.
```

- [ ] **Step 5: Prove configure/build on Windows and Linux before any DLNA wrapper code**

Run the dependency pin test again:

```bash
python tests/test_dlna_dependency_pin.py
```

Expected: PASS.

Configure/build the normal desktop target on Windows and Linux. Expected: `UPNP::Static` links with no sample/test targets required and no missing PThreads4W package on MSVC.

- [ ] **Step 6: Prove Android NDK can compile the pinned native library**

Run the repository's Android Qt/CMake configure for ARM64 with the reconciled Android branch. Expected: libupnp + ixml compile under the same NDK as the app without introducing Java jUPnP.

If the pinned native stack itself fails to compile on Android, stop this task with evidence. Do not silently switch libraries. A separate jUPnP adoption/license decision is required before changing architecture.

- [ ] **Step 7: Commit**

```bash
git add native/cmake/ColosseumPupnp.cmake native/CMakeLists.txt \
        .github/workflows/desktop-ci.yml .github/workflows/release-installer-smoke.yml \
        THIRD_PARTY_NOTICES.md licenses/libupnp-COPYING.txt tests/test_dlna_dependency_pin.py
git commit -m "build(dlna): pin libupnp 22.0.6"
```

---

## Task 3: Build the Read-Only Vault-to-DLNA Catalogue Projection

**Files:**
- Create: `native/dlna/DlnaTypes.h`
- Create: `native/dlna/DlnaCatalogueProjection.h`
- Create: `native/dlna/DlnaCatalogueProjection.cpp`
- Modify: `native/CMakeLists.txt`
- Create: `tests/auto/dlna/tst_dlna_catalogue.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
struct DlnaContainer {
    QString objectId;
    QString parentId;
    QString title;
    int childCount = 0;
};

struct DlnaMediaItem {
    QString objectId;
    QString parentId;
    QString title;
    QString filePath;
    QString mimeType;
    QString upnpClass;
    qint64 sizeBytes = 0;
    qint64 durationMs = -1;
};

struct DlnaBrowsePage {
    QList<DlnaContainer> containers;
    QList<DlnaMediaItem> items;
    quint32 totalMatches = 0;
    quint32 updateId = 0;
};
```

```cpp
class DlnaCatalogueProjection final : public QObject {
    Q_OBJECT
public:
    explicit DlnaCatalogueProjection(VaultIndex *index, QObject *parent = nullptr);

    DlnaBrowsePage browseChildren(const QString &objectId,
                                  quint32 startingIndex,
                                  quint32 requestedCount) const;
    std::optional<DlnaMediaItem> mediaItem(const QString &objectId) const;
    quint32 updateId() const;

signals:
    void changed();

private:
    VaultIndex *m_index = nullptr;
};
```

- [ ] **Step 1: Write failing projection tests against a real temporary VaultIndex**

Seed a `QTemporaryDir` `VaultIndex` with:

1. two videos in one group,
2. one video in another group,
3. one EPUB/book row,
4. one video row with `away = true`,
5. one rejected/no-video row.

Assertions:

```cpp
QCOMPARE(root.totalMatches, 2u);                 // two visible video groups
QCOMPARE(root.containers.first().parentId, "0");
QVERIFY(root.items.isEmpty());

const auto page = projection.browseChildren(groupId, 0, 1);
QCOMPARE(page.items.size(), 1);
QCOMPARE(page.totalMatches, 2u);
QVERIFY(page.items.first().objectId.startsWith("i:vault:"));
QVERIFY(!page.items.first().filePath.isEmpty());
QCOMPARE(page.items.first().upnpClass, "object.item.videoItem");
```

Also assert that calling `publish()`/`upsert()` on `VaultIndex` advances `projection.updateId()` and emits `changed()`.

Run only this test. Expected: FAIL because projection does not exist.

- [ ] **Step 2: Implement deterministic object IDs without another database**

Use:

```cpp
static QString groupObjectId(const QString &groupKey)
{
    const QByteArray digest = QCryptographicHash::hash(
        groupKey.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("g:") + QString::fromLatin1(digest);
}

static QString itemObjectId(const VaultIndex::FileRow &row)
{
    return QStringLiteral("i:") + row.id;
}
```

Root object ID is always `0`.

Maintain an in-memory group-ID-to-groupKey map rebuilt from `groupsForKind("video")` per browse call or from a revision-keyed snapshot. Do not persist it.

- [ ] **Step 3: Implement the V1 eligibility rule**

A row is exportable when all are true:

```cpp
row.kind == QStringLiteral("video")
&& !row.away
&& QFileInfo::exists(row.path)
&& row.admissionVerdict != QStringLiteral("RejectedNoVideo")
&& row.admissionVerdict != QStringLiteral("RejectedError")
&& row.admissionVerdict != QStringLiteral("RejectedTimeout")
```

An empty/unprobed admission verdict remains eligible: DLNA clients may support media the local probe has not classified yet.

- [ ] **Step 4: Implement the minimal MIME policy**

```cpp
static QString mimeForPath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp4" || ext == "m4v") return "video/mp4";
    if (ext == "mkv") return "video/x-matroska";
    if (ext == "webm") return "video/webm";
    if (ext == "avi") return "video/x-msvideo";
    if (ext == "mov") return "video/quicktime";
    return {};
}
```

Unknown MIME returns ineligible for V1 rather than lying with `application/octet-stream`.

- [ ] **Step 5: Use the existing revision as the SystemUpdate clock**

```cpp
quint32 DlnaCatalogueProjection::updateId() const
{
    return m_index ? quint32(m_index->revision() & 0xffffffffu) : 0u;
}
```

Connect `VaultIndex::changed` to `DlnaCatalogueProjection::changed`.

- [ ] **Step 6: Run tests and commit**

Expected: all catalogue tests PASS; existing Vault tests remain unchanged.

```bash
git add native/dlna native/CMakeLists.txt tests/auto/dlna/tst_dlna_catalogue.cpp tests/CMakeLists.txt
git commit -m "feat(dlna): project VaultIndex into media catalogue"
```

---

## Task 4: Generate Device, Service, SOAP, and DIDL XML in Colosseum-Owned Code

**Files:**
- Create: `native/dlna/DlnaDescription.h`
- Create: `native/dlna/DlnaDescription.cpp`
- Create: `native/dlna/DlnaDidlWriter.h`
- Create: `native/dlna/DlnaDidlWriter.cpp`
- Create: `native/dlna/DlnaSoap.h`
- Create: `native/dlna/DlnaSoap.cpp`
- Create: `tests/auto/dlna/tst_dlna_xml.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
namespace DlnaDescription {
QString rootDeviceXml(const QString &udn, const QString &friendlyName);
QString contentDirectoryScpd();
QString connectionManagerScpd();
}

namespace DlnaDidlWriter {
QString writeBrowseResult(const DlnaBrowsePage &page, const QUrl &baseMediaUrl);
}

namespace DlnaSoap {
QVariantMap parseArguments(const IXML_Document *request);
IXML_Document *makeResponse(const QString &action,
                            const QString &serviceType,
                            const QList<QPair<QString, QString>> &arguments);
}
```

- [ ] **Step 1: Write failing XML contract tests**

Verify root device XML includes exactly these service types:

```text
urn:schemas-upnp-org:device:MediaServer:1
urn:schemas-upnp-org:service:ContentDirectory:1
urn:schemas-upnp-org:service:ConnectionManager:1
```

Verify ContentDirectory SCPD contains:

```text
Browse
GetSystemUpdateID
GetSearchCapabilities
GetSortCapabilities
```

Verify ConnectionManager SCPD contains at least:

```text
GetProtocolInfo
GetCurrentConnectionIDs
GetCurrentConnectionInfo
```

Verify DIDL escapes XML characters in titles and emits:

```xml
<upnp:class>object.item.videoItem</upnp:class>
```

and a `<res>` with size, optional duration, URL, MIME type, and:

```text
DLNA.ORG_OP=01
```

Expected: FAIL.

- [ ] **Step 2: Generate XML using Qt, not copied GPL serializers**

Use `QXmlStreamWriter` for root device/DIDL generation. Use the UPnP AV service contracts to author the minimal SCPD strings locally. Do not copy Gerbera/Jellyfin/ReadyMedia XML builder code.

Use this protocolInfo shape:

```cpp
QString protocolInfoFor(const DlnaMediaItem &item)
{
    return QStringLiteral("http-get:*:%1:DLNA.ORG_OP=01").arg(item.mimeType);
}
```

Duration formatter:

```cpp
QString dlnaDuration(qint64 durationMs)
{
    if (durationMs < 0) return {};
    const qint64 totalSeconds = durationMs / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    const qint64 millis = durationMs % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}
```

- [ ] **Step 3: Parse action arguments without borrowing a server implementation**

Serialize libupnp's incoming `IXML_Document` to UTF-8 and parse with `QXmlStreamReader`, returning the action's immediate argument elements in a `QVariantMap`. Reject malformed XML and duplicate argument names.

Build action responses with libupnp's permissively licensed helper API `UpnpAddToActionResponse` rather than copying another MediaServer's SOAP response builder.

- [ ] **Step 4: Add deterministic Browse response helpers**

A successful Browse response must include:

```text
Result
NumberReturned
TotalMatches
UpdateID
```

`GetSearchCapabilities` and `GetSortCapabilities` return an empty string in V1. `GetSystemUpdateID` returns the projection's current `updateId()`.

- [ ] **Step 5: Run tests and commit**

```bash
git add native/dlna native/CMakeLists.txt tests/auto/dlna/tst_dlna_xml.cpp tests/CMakeLists.txt
git commit -m "feat(dlna): add UPnP AV XML contracts"
```

---

## Task 5: Own libupnp Global Lifecycle and Select Only a LAN Interface

**Files:**
- Create: `native/dlna/DlnaLanInterface.h`
- Create: `native/dlna/DlnaLanInterface.cpp`
- Create: `native/dlna/DlnaSdkRuntime.h`
- Create: `native/dlna/DlnaSdkRuntime.cpp`
- Create: `tests/auto/dlna/tst_dlna_lan_interface.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
struct DlnaLanInterface {
    QString interfaceName;
    QString displayName;
    QHostAddress address;
};

std::optional<DlnaLanInterface> selectDlnaLanInterface(
    const QList<QNetworkInterface> &interfaces);
```

```cpp
class DlnaSdkRuntime final : public QObject {
    Q_OBJECT
public:
    explicit DlnaSdkRuntime(QObject *parent = nullptr);
    bool start(const DlnaLanInterface &lan);
    void stop();
    bool running() const;
    QString lastError() const;
    QHostAddress address() const;
};
```

- [ ] **Step 1: Write failing interface-selection tests**

Cover at least:

- active Ethernet private IPv4 accepted,
- active Wi-Fi private IPv4 accepted,
- loopback rejected,
- down interface rejected,
- public IPv4 rejected,
- addressless interface rejected,
- IPv6 ignored in V1,
- deterministic choice when Wi-Fi + Ethernet both qualify.

Prefer Ethernet, then Wi-Fi, then other explicitly local/private non-loopback interfaces. Never choose `0.0.0.0`.

Expected: FAIL.

- [ ] **Step 2: Implement explicit local-address classification**

Accept RFC1918 private IPv4:

```text
10.0.0.0/8
172.16.0.0/12
192.168.0.0/16
```

Also accept IPv4 link-local `169.254.0.0/16` for direct LAN links. Do not treat cellular/public/CGN addresses as a sharing interface in V1.

- [ ] **Step 3: Wrap libupnp's process-global init exactly once**

`DlnaSdkRuntime::start()`:

1. choose the platform-correct interface name,
2. call `UpnpInit2(interfaceUtf8.constData(), 0)`,
3. verify success and `UpnpGetServerIpAddress()` matches the selected local address family/intent,
4. leave the SDK running for MediaServer and/or client registration.

`stop()` calls `UpnpFinish()` exactly once after all registered handles are released.

Do not call `UpnpInit2()` independently from both `DlnaMediaServer` and `DlnaClient`.

- [ ] **Step 4: Prove restartability**

Add a harness test where permitted by the host:

```text
start -> stop -> start -> stop
```

There must be no stale port/socket preventing the second start.

- [ ] **Step 5: Run tests and commit**

```bash
git add native/dlna native/CMakeLists.txt tests/auto/dlna/tst_dlna_lan_interface.cpp tests/CMakeLists.txt
git commit -m "feat(dlna): add LAN-only UPnP runtime"
```

---

## Task 6: Serve Opaque Media URLs with Real Byte-Range Semantics

**Files:**
- Create: `native/dlna/DlnaVirtualFileServer.h`
- Create: `native/dlna/DlnaVirtualFileServer.cpp`
- Create: `tests/auto/dlna/tst_dlna_virtual_file.cpp`
- Create: `tests/integration/dlna_http_probe.py`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
class DlnaVirtualFileServer final {
public:
    explicit DlnaVirtualFileServer(DlnaCatalogueProjection *catalogue);
    bool registerVirtualDirectory();
    void unregisterVirtualDirectory();
    QUrl mediaUrl(const QHostAddress &host, quint16 port,
                  const DlnaMediaItem &item) const;
};
```

- [ ] **Step 1: Write failing file-handle tests**

Using a temporary 64 KiB file with known bytes, assert:

- unknown object ID cannot open,
- a known ID opens read-only,
- read returns exact requested bytes,
- seek to an offset and read returns exact bytes,
- seek beyond EOF is rejected or clamps according to libupnp callback contract,
- close releases the QFile,
- object URL contains only encoded object identity, never the raw path.

Expected: FAIL.

- [ ] **Step 2: Register libupnp virtual-directory callbacks**

Use a fixed path:

```text
/colosseum/media/
```

Register GetInfo/Open/Read/Seek/Close callbacks through libupnp. Each open handle contains only:

```cpp
struct OpenMediaHandle {
    QFile file;
    qint64 size = 0;
    QByteArray mime;
};
```

Resolve the request token through `DlnaCatalogueProjection::mediaItem()` before opening the file.

There is no write path. If libupnp requires a write callback, return the documented failure value unconditionally.

- [ ] **Step 3: Make URLs opaque and traversal-proof**

URL shape:

```text
http://<selected-lan-ip>:<libupnp-port>/colosseum/media/<percent-encoded-object-id>
```

Decode exactly one path segment. Reject additional `/`, `..`, NUL, malformed percent escapes, and object IDs that do not resolve through the catalogue.

- [ ] **Step 4: Prove the actual integrated HTTP server honors HEAD and Range**

Do not settle for callback unit tests. Start a real libupnp webserver fixture and run:

```bash
python tests/integration/dlna_http_probe.py --url http://127.0.0.1:<fixture-port>/colosseum/media/<fixture-id>
```

The probe must assert:

```text
HEAD -> 200 with exact Content-Length and Content-Type
GET -> 200 with exact full bytes
GET Range: bytes=5-9 -> 206
Content-Range: bytes 5-9/<full-size>
Content-Length: 5
body == source[5:10]
```

If libupnp's integrated webserver does not produce correct 206 behavior from virtual seek callbacks, do not mask it. Replace only the media-resource HTTP path with a small Colosseum-owned `QTcpServer` range server while leaving libupnp responsible for SSDP/SOAP/device description. Preserve the same opaque URL and tests.

- [ ] **Step 5: Commit**

```bash
git add native/dlna native/CMakeLists.txt tests/auto/dlna/tst_dlna_virtual_file.cpp \
        tests/integration/dlna_http_probe.py tests/CMakeLists.txt
git commit -m "feat(dlna): serve Vault media with byte ranges"
```

---

## Task 7: Implement the Desktop MediaServer and UPnP AV Actions

**Files:**
- Create: `native/dlna/DlnaMediaServer.h`
- Create: `native/dlna/DlnaMediaServer.cpp`
- Create: `tests/auto/dlna/tst_dlna_media_server.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
class DlnaMediaServer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    DlnaMediaServer(DlnaSdkRuntime *runtime,
                    DlnaCatalogueProjection *catalogue,
                    QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool enabled);
    bool running() const;
    QString lastError() const;

signals:
    void enabledChanged();
    void runningChanged();
    void lastErrorChanged();

private:
    bool startServer();
    void stopServer();
    static int upnpCallback(Upnp_EventType type, const void *event, void *cookie);
};
```

- [ ] **Step 1: Write failing lifecycle tests**

Assert:

```text
constructed -> enabled=false, running=false
enabled=true with a valid LAN runtime -> running=true
enabled=false -> root device unregistered and running=false
enabled=true after stop -> starts again
missing LAN -> enabled remains true but running=false and lastError is honest
```

Use injected/wrapped libupnp calls where necessary so unit tests do not require a real multicast network.

- [ ] **Step 2: Persist one stable UDN**

Use `QSettings` category/key owned by DLNA internals, for example:

```text
[dlna]
deviceUuid=<uuid without urn:uuid prefix>
```

On first run create `QUuid::createUuid()`. Advertised UDN:

```text
uuid:<persisted-uuid>
```

Never derive UDN from IP address.

Friendly name:

```cpp
QStringLiteral("Colosseum on %1").arg(QHostInfo::localHostName())
```

- [ ] **Step 3: Register the root device and advertise it**

Build the root description in memory and call the pinned libupnp root-device registration API with `UPNPREG_BUF_DESC`.

After successful registration:

```cpp
UpnpSendAdvertisement(deviceHandle, 1800);
```

On stop, unregister the root device before releasing the SDK runtime.

- [ ] **Step 4: Route ContentDirectory actions**

For `Browse`:

- require `ObjectID`, `BrowseFlag`, `StartingIndex`, `RequestedCount`,
- support `BrowseDirectChildren` for root/group,
- support `BrowseMetadata` for known group/item,
- reject unknown object IDs with a proper UPnP action error,
- serialize the result with `DlnaDidlWriter`,
- return `NumberReturned`, `TotalMatches`, `UpdateID`.

For capabilities:

```text
GetSearchCapabilities -> SearchCaps=""
GetSortCapabilities -> SortCaps=""
GetSystemUpdateID -> Id=<projection updateId>
```

- [ ] **Step 5: Route ConnectionManager actions truthfully**

`GetProtocolInfo` Source must list only MIME types V1 actually exports, for example:

```text
http-get:*:video/mp4:DLNA.ORG_OP=01,
http-get:*:video/x-matroska:DLNA.ORG_OP=01,
http-get:*:video/webm:DLNA.ORG_OP=01,
http-get:*:video/x-msvideo:DLNA.ORG_OP=01,
http-get:*:video/quicktime:DLNA.ORG_OP=01
```

Sink is empty.

Return no active connection IDs in V1. For `GetCurrentConnectionInfo`, return the protocol-defined no-such-connection error when the requested ID is unknown rather than inventing state.

- [ ] **Step 6: React to LAN interface changes without duplicate servers**

Observe network changes with Qt. Debounce changes, recompute the selected LAN interface, and when the selected address/name changes:

```text
unregister old device -> stop old runtime -> start runtime on new LAN -> register one device -> advertise
```

Do nothing if the effective interface/address did not change.

- [ ] **Step 7: Run tests and commit**

```bash
git add native/dlna native/CMakeLists.txt tests/auto/dlna/tst_dlna_media_server.cpp tests/CMakeLists.txt
git commit -m "feat(dlna): add desktop UPnP AV MediaServer"
```

---

## Task 8: Wire the One Settings Toggle to the Desktop Server

**Files:**
- Modify: `qml/ContentPreferences.qml`
- Modify: `qml/SettingsPage.qml`
- Modify: `qml/Main.qml`
- Modify: `native/main.cpp`
- Create: `tests/qml/tst_dlna_settings.qml`
- Modify the repository's QML test registration as required by its current harness.

**Interfaces:**
- `ContentPreferences.dlnaSharingEnabled: bool`, persisted, default false.
- QML context property: `Dlna` -> `DlnaMediaServer*`.

- [ ] **Step 1: Write the failing settings test**

The QML test must prove:

```text
default dlnaSharingEnabled == false
click/keyboard trigger changes it to true
second trigger changes it back to false
recreating ContentPreferences against the same temporary INI restores the saved value
```

Also assert the switch's accessibility name is `Share Colosseum over DLNA`.

Expected: FAIL.

- [ ] **Step 2: Add the persisted preference beside showExplicit**

Modify `qml/ContentPreferences.qml`:

```qml
property alias dlnaSharingEnabled: store.dlnaSharingEnabled

property Settings settingsStore: Settings {
    id: store
    category: "content"
    property bool showExplicit: false
    property bool dlnaSharingEnabled: false
    onShowExplicitChanged: root.changed()
    onDlnaSharingEnabledChanged: root.changed()
}
```

Keep the existing injected `settingsLocation` test seam.

- [ ] **Step 3: Add exactly one new settings row**

In `qml/SettingsPage.qml`, reuse the existing house toggle visual language. Text:

```text
Share Colosseum over DLNA
Make playable media in Colosseum available to devices on this Wi-Fi/LAN. Anyone on this local network can browse and play it.
```

The toggle reads/writes only:

```qml
root.preferences.dlnaSharingEnabled
```

Accessible name:

```text
Share Colosseum over DLNA
```

No extra modal, port field, password field, device list, or advanced section.

- [ ] **Step 4: Compose DLNA owners in native/main.cpp**

After `VaultIndex` exists and before QML loads:

```cpp
auto *dlnaRuntime = new DlnaSdkRuntime(&app);
auto *dlnaCatalogue = new DlnaCatalogueProjection(vaultIndex, &app);
auto *dlnaServer = new DlnaMediaServer(dlnaRuntime, dlnaCatalogue, &app);
engine.rootContext()->setContextProperty(QStringLiteral("Dlna"), dlnaServer);
```

Desktop and Android may both compile these types, but Android does not automatically enable MediaServer sharing in V1.

- [ ] **Step 5: Bind persisted user intent to the server**

In `qml/Main.qml`, bind only when the native object exists in that build/harness:

```qml
Binding {
    target: (typeof Dlna !== "undefined") ? Dlna : null
    property: "enabled"
    value: contentPreferences.dlnaSharingEnabled
    when: target !== null
}
```

Do not start the server from `SettingsPage.qml`; QML stores intent, native owner performs lifecycle.

- [ ] **Step 6: Prove OFF means zero server**

Run the QML test plus media-server lifecycle test. Launch production once with a clean settings store and verify no SSDP advertisement occurs before the user enables the toggle.

- [ ] **Step 7: Commit**

```bash
git add qml/ContentPreferences.qml qml/SettingsPage.qml qml/Main.qml native/main.cpp \
        tests/qml/tst_dlna_settings.qml
git commit -m "feat(settings): add DLNA LAN sharing toggle"
```

---

## Task 9: Prove the Desktop Server End to End With Its Own Control Point

**Files:**
- Create: `tests/integration/dlna_probe.cpp` or equivalent Qt test executable
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/colosseum-test-verification.md`

**Interfaces:**
- The probe uses libupnp's client/control-point API against the real Colosseum server fixture.

- [ ] **Step 1: Write the failing end-to-end probe**

The fixture must create a temporary `VaultIndex`, publish one real local MP4 fixture row, enable a real `DlnaMediaServer`, and then from a separate client handle:

1. discover `urn:schemas-upnp-org:device:MediaServer:1`,
2. retrieve/parse the device description,
3. call ContentDirectory `Browse` on object `0`,
4. browse the returned group,
5. obtain the item's `<res>` URL,
6. request a byte range and verify exact fixture bytes.

Expected: FAIL until all prior layers are connected correctly.

- [ ] **Step 2: Add update propagation proof**

After the first Browse, mutate the `VaultIndex` with one additional eligible video and call `GetSystemUpdateID`. Assert the ID changed and a subsequent Browse returns the new count.

- [ ] **Step 3: Add shutdown proof**

Toggle the server off, then verify:

- media resource URL no longer serves,
- root device has been unregistered,
- a new discovery cycle does not rediscover the fixture after expiry/byebye processing.

- [ ] **Step 4: Register/document the test**

Add the probe to CTest with a reasonable timeout and mark it as a local-network integration test if multicast cannot run reliably inside all hosted CI environments. The core SOAP/catalogue/range tests remain ordinary CI tests regardless.

- [ ] **Step 5: Manual desktop interop gate**

On a real LAN, with one actual video in Vault and one completed video Download:

```text
VLC -> discover Colosseum -> browse -> play -> seek to ~50% -> pause/resume
Kodi -> discover Colosseum -> browse -> play -> seek
Toggle OFF -> Colosseum disappears / new playback connections fail
Toggle ON -> same UDN/friendly server identity returns
```

Record exact client versions and any reproduced quirk before adding compatibility code.

- [ ] **Step 6: Commit**

```bash
git add tests/integration tests/CMakeLists.txt docs/colosseum-test-verification.md
git commit -m "test(dlna): prove MediaServer discovery browse and range playback"
```

---

## Task 10: Add the Shared Native DLNA Control Point for Android

**Files:**
- Create: `native/dlna/DlnaClient.h`
- Create: `native/dlna/DlnaClient.cpp`
- Create: `tests/auto/dlna/tst_dlna_client.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `native/main.cpp`

**Interfaces:**

```cpp
class DlnaClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)

public:
    DlnaClient(DlnaSdkRuntime *runtime, QObject *parent = nullptr);
    QVariantList servers() const;

    Q_INVOKABLE void discover();
    Q_INVOKABLE void cancelDiscovery();
    Q_INVOKABLE void browse(const QString &serverUdn, const QString &objectId);

signals:
    void serversChanged();
    void discoveryFinished();
    void browseReady(QString serverUdn, QString objectId, QVariantList rows);
    void browseFailed(QString serverUdn, QString objectId, QString error);
};
```

- [ ] **Step 1: Write failing client tests against the real desktop fixture**

Assert discovery deduplicates by UDN, ignores non-MediaServer devices, and stores only the information required to call the server later:

```text
udn
friendlyName
locationUrl
contentDirectoryControlUrl
```

Assert `browse()` converts DIDL-Lite into rows with:

```text
objectId
parentId
title
isContainer
resourceUrl
mimeType
sizeBytes
durationMs
```

Expected: FAIL.

- [ ] **Step 2: Register one libupnp client handle through DlnaSdkRuntime**

Use `UpnpRegisterClient` and `UpnpSearchAsync` for:

```text
urn:schemas-upnp-org:device:MediaServer:1
```

Keep discovery time-bounded, e.g. 3 seconds. A second discovery cancels/replaces the old generation so stale callbacks cannot repopulate the model.

- [ ] **Step 3: Implement Browse asynchronously**

Construct a ContentDirectory Browse action using libupnp helper APIs. Never block the QML/GUI thread waiting for SOAP.

Reject resource URLs that are not ordinary HTTP URLs returned by the selected discovered server. Final Android relay validation is stricter and happens before playback.

- [ ] **Step 4: Expose client to QML only on platforms that consume it**

In `native/main.cpp` on Android:

```cpp
auto *dlnaClient = new DlnaClient(dlnaRuntime, &app);
engine.rootContext()->setContextProperty(QStringLiteral("DlnaClient"), dlnaClient);
```

Desktop may construct no client in production unless a future product surface needs it; the integration test can construct one directly.

- [ ] **Step 5: Run tests and commit**

```bash
git add native/dlna native/main.cpp native/CMakeLists.txt tests/auto/dlna/tst_dlna_client.cpp tests/CMakeLists.txt
git commit -m "feat(dlna): add native MediaServer discovery client"
```

---

## Task 11: Make Android SSDP Discovery Legal and Lifecycle-Safe

**Files:**
- Create: `native/platform/android/src/org/colosseum/dlna/DlnaNetworkAccess.java`
- Modify: `native/platform/android/AndroidManifest.xml`
- Create: `native/dlna/DlnaAndroidNetwork.h`
- Create: `native/dlna/DlnaAndroidNetwork.cpp`
- Create: `tests/test_android_dlna_network_contract.py`
- Modify: `native/CMakeLists.txt`

**Interfaces:**

Java:

```java
public final class DlnaNetworkAccess {
    public static boolean acquireMulticastLock();
    public static void releaseMulticastLock();
    public static boolean hasLocalNetworkPermission();
}
```

C++ wrapper:

```cpp
namespace DlnaAndroidNetwork {
bool beginDiscoveryWindow();
void endDiscoveryWindow();
bool localNetworkAllowed();
}
```

- [ ] **Step 1: Write the failing Android source-contract test**

Assert source contains:

```text
android.permission.CHANGE_WIFI_MULTICAST_STATE
WifiManager.MulticastLock
setReferenceCounted(false)
acquire()
release()
```

Also assert the current SDK-36 manifest does **not** add `android.permission.ACCESS_LOCAL_NETWORK` prematurely.

Expected: FAIL.

- [ ] **Step 2: Add Wi-Fi multicast permission**

Add to `native/platform/android/AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.CHANGE_WIFI_MULTICAST_STATE" />
```

Keep existing Internet access behavior provided by Qt/application packaging.

- [ ] **Step 3: Implement a short-lived multicast lock**

In Java:

```java
WifiManager wifi = (WifiManager) activity.getApplicationContext()
    .getSystemService(Context.WIFI_SERVICE);
multicastLock = wifi.createMulticastLock("colosseum-dlna-discovery");
multicastLock.setReferenceCounted(false);
multicastLock.acquire();
```

Release on discovery completion, timeout, cancellation, or app lifecycle suspension. Never hold it for general playback.

- [ ] **Step 4: Gate Android 17+ local-network permission without breaking SDK 36**

Current targetSdk 36 behavior: do not request/add `ACCESS_LOCAL_NETWORK`.

Implement the C++/Java seam so that when the project later targets API 37+, `localNetworkAllowed()` can check/request Android's local-network runtime permission before starting SSDP. Add a source-contract assertion that the target-SDK gate is explicit. Do not fake approval when permission is absent.

- [ ] **Step 5: Connect discovery lifecycle**

`DlnaClient::discover()` on Android:

```text
localNetworkAllowed -> acquire multicast lock -> UpnpSearchAsync -> release on terminal discovery generation
```

`cancelDiscovery()` and application suspension must release the lock even if libupnp never returns another callback.

- [ ] **Step 6: Run test and commit**

```bash
python tests/test_android_dlna_network_contract.py
```

Expected: PASS.

```bash
git add native/platform/android native/dlna native/CMakeLists.txt tests/test_android_dlna_network_contract.py
git commit -m "feat(android): enable lifecycle-safe DLNA discovery"
```

---

## Task 12: Add a Constrained Android Loopback Relay for Media3

**Files:**
- Create: `native/dlna/DlnaAndroidRelay.h`
- Create: `native/dlna/DlnaAndroidRelay.cpp`
- Create: `tests/auto/dlna/tst_dlna_android_relay.cpp`
- Create: `tests/test_android_dlna_cleartext_contract.py`
- Modify: `native/CMakeLists.txt`
- Keep unchanged: `native/platform/android/res/xml/network_security_config.xml`

**Interfaces:**

```cpp
class DlnaAndroidRelay final : public QObject {
    Q_OBJECT
public:
    explicit DlnaAndroidRelay(QObject *parent = nullptr);

    QUrl createPlaybackUrl(const QUrl &remoteUrl,
                           const QHostAddress &discoveredServerAddress,
                           int ttlSeconds = 300);
    void revokeAll();

private:
    QTcpServer *m_server = nullptr;
};
```

- [ ] **Step 1: Write failing security/range tests**

Create a local fake remote media server and assert:

- relay listens only on `127.0.0.1`,
- only GET and HEAD are accepted,
- valid `Range` is forwarded,
- 206/Content-Range/Content-Length/body are preserved,
- arbitrary user-supplied URL cannot create a relay session,
- public IP target is rejected,
- target host resolving outside the discovered server's LAN is rejected,
- redirect to public IP is rejected,
- unknown/expired token returns 404/410,
- `revokeAll()` closes sessions.

Expected: FAIL.

- [ ] **Step 2: Keep Android's network security config unchanged**

Add `tests/test_android_dlna_cleartext_contract.py` asserting the file still contains:

```xml
<base-config cleartextTrafficPermitted="false" />
<domain includeSubdomains="false">127.0.0.1</domain>
```

and does not introduce a broad cleartext allow rule.

- [ ] **Step 3: Make relay sessions capability-based**

`createPlaybackUrl()` may only be called from the DLNA client path after a resource came from a discovered server. Validate:

1. scheme is `http`,
2. no embedded credentials,
3. destination port is valid,
4. host resolves to the same active private/local subnet as the discovered server,
5. resolved address is not loopback/public unless it is the exact validated discovered server target,
6. path/query are preserved but fragment is dropped.

Generate a cryptographically random per-session token with `QRandomGenerator::system()` or equivalent Qt secure source.

Returned URL:

```text
http://127.0.0.1:<ephemeral-port>/dlna/<opaque-token>
```

- [ ] **Step 4: Forward only the headers playback needs**

Forward:

```text
Range
If-Range
User-Agent (Colosseum controlled)
```

Do not forward arbitrary client-supplied Host/Cookie/Authorization headers. Strip hop-by-hop response headers and set the local Host independently.

Use bounded buffers/backpressure; do not read the entire movie into RAM.

- [ ] **Step 5: Feed Media3 the loopback URL, not the LAN URL**

When Android selects a remote DLNA media item:

```cpp
const QUrl local = relay->createPlaybackUrl(remoteResourceUrl, serverAddress);
```

Pass `local` through the existing Android Media3 playback path exactly like other HTTP sources. Do not change `Media3PlayerHost` cleartext policy or instantiate a second player.

- [ ] **Step 6: Run tests and commit**

```bash
python tests/test_android_dlna_cleartext_contract.py
```

Run the C++ relay tests and Android compile contracts. Expected: all PASS and `network_security_config.xml` remains globally strict.

```bash
git add native/dlna native/CMakeLists.txt tests/auto/dlna/tst_dlna_android_relay.cpp \
        tests/test_android_dlna_cleartext_contract.py
git commit -m "feat(android): relay DLNA playback through loopback"
```

---

## Task 13: Add the Minimal `Nearby Colosseum` Android Surface

**Files:**
- Modify: `qml/VaultPage.qml`
- Create: `qml/NearbyColosseum.qml`
- Modify: `native/main.cpp` if a thin playback handoff context object is required
- Create: `tests/qml/tst_nearby_colosseum.qml`
- Modify QML test registration for the current repository harness.

**Interfaces:**
- Reads `DlnaClient.servers`.
- Calls `DlnaClient.discover()` when the relevant Vault/local-media surface becomes active.
- Calls `DlnaClient.browse(serverUdn, objectId)`.
- Sends a selected remote resource through `DlnaAndroidRelay` and the existing player navigation contract.

- [ ] **Step 1: Write the failing QML test with a fake DlnaClient**

Assertions:

```text
zero servers -> Nearby Colosseum surface hidden
one server -> one Nearby Colosseum entry visible
server title displayed from friendlyName
select server -> browse(serverUdn, "0") invoked
select container -> browse(serverUdn, containerId) invoked
select media item -> existing playback handoff invoked once
```

There is no pairing UI, account UI, download button, sync button, or settings button in this component.

Expected: FAIL.

- [ ] **Step 2: Implement one conditional entry point**

`qml/NearbyColosseum.qml` owns only the remote navigation state. It should use the same card/list vocabulary already used by Vault rather than inventing a second application shell.

At the Vault surface, include it only on Android/current client-capable builds and only when at least one server exists.

- [ ] **Step 3: Keep remote state ephemeral**

Do not call `VaultIndex`, `VaultLibrary`, `LocalDownloads`, Account sync, or any persistence store with remote rows. Closing the remote surface may discard its current browse model.

- [ ] **Step 4: Wire playback into existing Media3 path**

The selected item flow is:

```text
DlnaClient DIDL row
-> validate selected server/resource
-> DlnaAndroidRelay local URL
-> existing PlayerPage / AndroidMedia3Item source
```

No separate DLNA player page.

- [ ] **Step 5: Run QML + Android tests and commit**

```bash
git add qml/VaultPage.qml qml/NearbyColosseum.qml tests/qml/tst_nearby_colosseum.qml native/main.cpp
git commit -m "feat(android): browse nearby Colosseum libraries"
```

---

## Task 14: Final Qualification, Regression Defenses, and Documentation

**Files:**
- Create: `docs/dlna.md`
- Modify: `docs/colosseum-test-verification.md`
- Modify: `THIRD_PARTY_NOTICES.md` only if final packaged dependency inventory changed during implementation
- Modify relevant CI workflow(s) only to register stable new tests; do not weaken existing gates.

- [ ] **Step 1: Run focused automated suite**

Run every new DLNA test individually first, then as a group:

```text
dlna dependency pin
dlna catalogue projection
dlna XML/SOAP/DIDL
dlna LAN interface/runtime
dlna virtual file/range
dlna MediaServer lifecycle
dlna end-to-end probe
dlna client
dlna Android multicast contract
dlna Android cleartext contract
dlna Android relay
settings QML
Nearby Colosseum QML
```

No test may depend on public Internet services or external media metadata.

- [ ] **Step 2: Run full relevant desktop regression suite**

Run normal Windows and Linux configure/build/CTest gates used by current CI. Pay special attention to:

- Vault tests,
- StreamServer/torrent playback tests,
- HTTP header/range tests,
- Settings/QML keyboard tests,
- packaging/release smoke.

DLNA must not change the existing Stremio/Colosseum stream-server port 11470 behavior or libtorrent's unrelated UPnP/NAT policy.

- [ ] **Step 3: Run Android qualification**

Build the ARM64 APK from the reconciled Android branch state and run the existing Android Media3/platform contract suite plus the new DLNA tests.

On a physical Android device on the same Wi-Fi as desktop Colosseum:

```text
Desktop sharing OFF -> Android sees no server
Desktop sharing ON -> Android discovers Colosseum
Browse -> select a video -> playback starts
Seek to 50% -> playback resumes from requested range
Background/foreground Android -> no leaked multicast lock, playback remains coherent
Turn desktop sharing OFF -> new browse/play requests fail honestly
```

- [ ] **Step 4: Real-client compatibility matrix**

At minimum qualify:

```text
Windows Colosseum server -> Android Colosseum
Windows Colosseum server -> VLC
Windows Colosseum server -> Kodi
Linux Colosseum server -> Android Colosseum
```

For every failure, capture the actual SSDP/SOAP/HTTP exchange before adding a compatibility special case. Compatibility code requires a reproduced device/client reason.

- [ ] **Step 5: Security verification**

Prove all of these with logs/tests:

```text
server binds selected local interface, not public/WAN
no router port mapping requested
raw filesystem paths never appear in advertised media URLs
unknown object IDs cannot read arbitrary files
Android relay binds only 127.0.0.1
Android relay rejects public/arbitrary targets and unsafe redirects
global Android cleartext policy remains false except loopback
toggle OFF unregisters and closes serving paths
```

- [ ] **Step 6: Write operator/user documentation**

`docs/dlna.md` should state in plain language:

```text
Settings -> Share Colosseum over DLNA
OFF by default
Shares playable local Colosseum media only to the same local network
Anyone on that LAN may browse/play while enabled
No Internet sharing, pairing, or transcoding in V1
Android Colosseum can discover nearby desktop Colosseum servers
```

Document the exact pinned libupnp version/commit and the reference-only license boundary.

- [ ] **Step 7: Review the final diff for scope creep**

Reject any accidental introduction of:

```text
DLNA database/scanner
transcoder
account dependency
pairing code
port-forwarding
broad Android cleartext allow
raw path URLs
GPL source copied into MIT-owned files
```

- [ ] **Step 8: Final commit**

```bash
git add docs/dlna.md docs/colosseum-test-verification.md THIRD_PARTY_NOTICES.md
git commit -m "docs(dlna): document LAN sharing and qualification"
```

- [ ] **Step 9: Verification-before-completion gate**

Use `superpowers:verification-before-completion` before claiming the feature is done. Report exact build/test commands, pass counts, physical-device/client matrix, branch SHA, and any remaining known limitation. Do not declare success from static/source-contract tests alone.

---

## Expected Final Product

A user sees one new switch in Settings. When it is OFF, nothing is shared. When it is ON, Colosseum advertises one LAN-only MediaServer backed directly by the existing Vault index, including completed Downloads already represented there. TVs/VLC/Kodi can browse and direct-play supported videos. Android Colosseum discovers the same server through `Nearby Colosseum`, browses it without importing its rows, and plays through the existing Media3 stack via a constrained loopback range relay.

The protocol engine comes from pinned BSD-licensed libupnp 22.0.6. The MediaServer/product semantics remain original Colosseum code. GPL MediaServer projects remain research references and donate zero source code.