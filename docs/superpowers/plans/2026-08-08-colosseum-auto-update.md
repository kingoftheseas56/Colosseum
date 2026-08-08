# Colosseum Auto-Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a native Colosseum Update page that discovers signed stable GitHub Releases, notifies through the Colosseum taskbar, downloads and verifies the installer with resume support, safely restarts into a rollback-capable NSIS update, and preserves the latest release chronicle afterward.

**Architecture:** C++ owns semantic versions, GitHub transport, manifest trust, resumable files, cached state, and installer handoff; QML only renders a typed `UpdateService` contract. A signed inert manifest supplies copy and hashed artwork to local QML templates. The existing NSIS package gains a side-by-side update mode and the release publisher remains draft-only until manifest, asset, installer, rollback, and clean-machine gates pass.

**Tech Stack:** C++17, Qt 6.11.1 Core/Network/QML/Quick/Concurrent, OpenSSL Ed25519 through `OpenSSL::Crypto`, QML/Qt Quick Test, CMake/Ninja/MSVC 2022, NSIS MUI2, Python 3 standard library, GitHub Releases REST API.

**Planning protocol:** Prepared and self-reviewed with the Brotherhood `brotherhood-writing-plans` skill at `../runtime/claude/skills/brotherhood-writing-plans/SKILL.md`. Brotherhood Rule 28 governs execution: work directly on `master`; a worktree requires Hemanth's explicit approval.

## Global Constraints

- Work directly on `master`; do not create a branch, worktree, second build tree, or separate installed instance without Hemanth's explicit approval.
- Canonical release versions are three-component `X.Y.Z`; tags are `vX.Y.Z`; display copy may omit a trailing `.0` only after parsing.
- Stable published GitHub Releases are the only automatic channel. Drafts, prereleases, bare tags, malformed releases, and unsigned releases remain invisible.
- Check after the shell is usable and every six hours while open; manual **Check again** bypasses the time gate.
- Never download the installer until the user clicks **Download update** and never install until **Restart and update**.
- Signature verification covers the exact UTF-8 manifest bytes before JSON parsing. The release private key never enters the repository or installer.
- Remote release presentation is inert: whitelisted text/numbers/template IDs plus hashed PNG/WebP asset names; no remote QML, JavaScript, HTML, shaders, fonts, commands, or automatically opened URLs.
- Stream downloads to the update cache; never hold an installer in memory or write it into the program tree.
- The taskbar badge remains until installation; its pulse stops after opening the Update page. Meaning never depends on animation or gold color alone.
- User libraries, downloads, progress, settings, and caches stay under `QStandardPaths`/`QSettings`, outside the replaceable program payload.
- Update mode never invokes uninstall. The old payload remains recoverable until the new shell records one successful boot.
- Use only HTTPS GitHub release delivery in production; loopback HTTP is allowed only through injected test configuration.
- Declare the exact additive `native/CMakeLists.txt` and `qml/Main.qml` changes in Brotherhood coordination before editing those shared files.
- Run deterministic tests inside each task. Run the full app build, unit label, QML gate, installer matrix, and clean-machine update proof before release.
- Every automated runtime replay uses `lanista session run` only: its unique pipe and tagged AppData/cache root are mandatory. Never drive the daily app or its default pipe.
- Update runtime fixtures use a test-only compiled public key and committed signed fixture bytes; the release private key remains outside the repository. `package_release.sh` must reject a build configured with `COLOSSEUM_UPDATE_TESTING=ON`.
- Lanista-visible QML names use the `colosseumUpdate...` namespace. Every wait is `ui-wait-for` on a named property; no sleeps and no bare QML `id` targets.
- Commit only the task's files by explicit path, verify the committed artifact, and push `master` after every task.

---

## File map

### Native update units

- Create `native/update/UpdateVersion.h/.cpp` — strict canonical parsing, comparison, and display formatting.
- Create `native/update/UpdateManifest.h/.cpp` — inert manifest data model and post-signature JSON/schema parser.
- Create `native/update/UpdateTrust.h/.cpp` — Ed25519 raw-byte verification plus SHA-256 helpers.
- Create `native/update/UpdatePublicKey.h` — embedded 32-byte production public key only.
- Create `native/update/UpdateTestPublicKey.h` — test-build-only 32-byte public key for Lanista fixture verification; never selected by a release build.
- Create `native/update/UpdateReleaseClient.h/.cpp` — GitHub latest-release discovery, ETag, exact asset selection, manifest/signature fetch, and trust gate.
- Create `native/update/UpdateCache.h/.cpp` — bounded update-cache layout, atomic state writes, safe path checks, chronicle/artwork adoption, and cleanup.
- Create `native/update/UpdateDownload.h/.cpp` — streamed `.part` download, Range/ETag resume, progress, cancel, and final size/hash verification.
- Create `native/update/UpdateInstallBridge.h/.cpp` — installed-build eligibility, detached installer launch, startup result parsing, healthy-boot backup cleanup.
- Create `native/update/UpdateService.h/.cpp` — typed QML state machine and six-hour policy coordinating all units.
- Modify `native/main.cpp` — compiled version, update context property, installed/dev configuration, post-shell automatic checks, healthy-boot acknowledgement.
- Modify `native/CMakeLists.txt` — `project(... VERSION 1.1.0)`, update sources, compile definition, OpenSSL crypto link, harness targets.

### QML surface

- Create `assets/icons/update.svg` — explicit monochrome update glyph suitable for Qt SVG rendering.
- Create `qml/UpdatePage.qml` — approved full-page state/action shell.
- Create `qml/update/UpdateReleaseHero.qml` — release title, summary, version, size, progress, and primary action.
- Create `qml/update/UpdateHighlightCard.qml` — local renderer for feature/statistic/before-after/milestone models.
- Modify `qml/Taskbar.qml` — permanent Update icon, gold availability badge, unseen pulse, active underline, accessible label.
- Modify `qml/Main.qml` — mutually exclusive Update loader, open/close route, taskbar bindings, update-seen event.

### Installer and release tooling

- Modify `scripts/installer/colosseum.nsi` — normal install plus silent side-by-side update/rollback path.
- Modify `scripts/installer/package_release.sh` — strict version/tag/build checks and clean output contract.
- Create `scripts/update/generate_update_manifest.py` — deterministic raw manifest bytes, asset hashes, detached Ed25519 signature.
- Create `scripts/update/verify_update_release.py` — local/draft release verification and exact asset/digest checks.
- Create `scripts/update/update-manifest-v1.schema.json` — publishing-time structural validation source.
- Modify `scripts/publish_app_release.py` — draft-only multi-asset upload, correct MIME types, no replacement of published assets.
- Create `release/presentation/1.1.0.json` — bootstrap release copy rendered by the approved local templates.

### Deterministic and integration proof

- Create `tests/update_version_harness.cpp`.
- Create `tests/update_manifest_trust_harness.cpp`.
- Create `tests/update_release_client_harness.cpp`.
- Create `tests/update_download_harness.cpp`.
- Create `tests/update_service_harness.cpp`.
- Create `tests/update_install_bridge_harness.cpp`.
- Create `tests/qml/tst_update_page.qml`.
- Create `tests/test_update_taskbar_p0.ps1`.
- Create `tests/test_update_data_boundary.ps1`.
- Create `tests/update_release_tooling_test.py`.
- Create `tests/installer/update_matrix.ps1`.
- Create `tests/lanista_scenarios/update_available.json` and `tests/lanista_scenarios/update_up_to_date.json`.
- Create `tests/lanista_fixtures/update-available/` and `tests/lanista_fixtures/update-up-to-date/` — Task 4 cache-shaped, production-inert data signed by the test key.
- Create `tests/test_update_lanista.ps1` — isolated session runner for both update states.
- Modify `tests/CMakeLists.txt` and `docs/colosseum-test-verification.md` with every new registered gate.
- Modify `docs/colosseum-lanista-verification.md` with the update automation surfaces and replay contract when they land.

---

## Brotherhood Lanista runtime coverage

### Slice L1: A verified cached update is visible and opens from the taskbar

**Purpose:** Prove in a disposable running Colosseum that the quiet Update taskbar signal opens the approved page and presents a verified release, not merely that its QML components compile.

**Dependencies:** Tasks 2, 4, 5, 6, 7, and 8. Task 2 supplies the test-build public key; Task 4 defines the cache layout; Tasks 6–7 add the named surfaces; Task 8 wires the real service into the real shell.

**Implementation guidance:** In a `COLOSSEUM_UPDATE_TESTING=ON` build only, `UpdateTrust` selects `UpdateTestPublicKey.h`; the normal build uses only `UpdatePublicKey.h`. The committed `update-available` seed contains a signed manifest, signature, cached presentation, and no installer payload. Add these stable automation surfaces: `colosseumUpdateTaskbarButton`, `colosseumUpdateBadge`, `colosseumUpdatePage`, `colosseumUpdatePrimaryAction`, `colosseumUpdateStatusText`, `colosseumUpdateProgress`, and `colosseumUpdateHighlights`. `colosseumUpdatePage` exposes read-only `automationState` and `automationVersion` properties bound to the real `Updates` object.

**Behavior to preserve:** A source-tree/development launch still performs no automatic GitHub check; the normal taskbar pages remain mutually exclusive; no fixture, test key, or test-mode switch can enter a packaged installer.

**Baseline:** Before Tasks 6–8, launch an isolated session with the fixture seed and record that `colosseumUpdateTaskbarButton` / `colosseumUpdatePage` are absent. Preserve the failing scenario output under `artifacts/lanista-sessions/`.

**Focused tests:**

- **Qt Test:** `update_manifest_trust_harness`, `update_release_client_harness`, `update_cache_download_harness`, `update_service_harness`, and `update_install_bridge_harness`; the service cache case must prove the signed fixture reaches `Available` without a network request.
- **Qt Quick Test:** `tst_update_page.qml` verifies the exact state/action mapping with its fake service; its expectations use the same `automationState` names as the runtime page.
- **Existing harnesses:** `tests/test_update_taskbar_p0.ps1`, `tests/test_fullscreen_controls_p0.ps1`, and the new `tests/test_update_lanista.ps1`. The existing `tests/test_lanista.ps1` remains the bridge-contract gate, not the update feature replay.
- **Negative control:** flip one byte in the seeded manifest signature and assert the service does not expose `Available`; rename `colosseumUpdateTaskbarButton` in the scenario once and record the resulting `NO_SUCH_ITEM` before restoring it.

**Test seam status:** migration required until Task 8 adds the scenario runner and records it in both verification ledgers; available afterward.

**Lanista actions:** `lanista session run tests/lanista_scenarios/update_available.json --exe native/build-msvc/colosseum.exe --tag updater-available --drive --seed tests/lanista_fixtures/update-available --verbose`. The scenario must: `ping`; use `get-state` to assert both `appDataRoot` and `cacheRoot` contain the session tag; `ui-wait-for bootSplash.visible == false`; `ui-query colosseumUpdateTaskbarButton` for visible/enabled/not-clipped; `ui-click colosseumUpdateTaskbarButton`; `ui-wait-for colosseumUpdatePage.visible == true`; `qml-get` the page's `automationState` / `automationVersion` and status-text `text`; and grab the whole window after the page opens. It must never call the download/install action.

**Completion signal:** `colosseumUpdatePage.visible == true` followed by `colosseumUpdatePage.automationState == "Available"`; these are exact `ui-wait-for` comparisons, not elapsed-time waits.

**State / events / probes:** `get-state` proves isolation; `qml-get colosseumUpdatePage` returns `automationState: "Available"` and `automationVersion: "1.1.1"`; `qml-get colosseumUpdateStatusText` returns the user-facing available copy. Do not claim typed update events: the ledger lists no such event plane.

**Visual evidence:** the session manifest, stdout/stderr, and whole-window grab in `artifacts/lanista-sessions/<id>/` show the gold taskbar badge, opened Update page, version, and feature cards. dHash may flag broad drift only; Hemanth remains the aesthetic judge.

**Regression paths:** close Update, open Downloads, reopen Update, and confirm the page is still exclusive; navigate away and back without losing the cached chronicle.

**Evidence artifacts:** the two session directories, their `colosseum.session.v1` manifests, scenario stdout/stderr, and the Task 11 eyes-on gallery.

**Bridge status:** available — `session run`, `get-state`, `ui-query`, `ui-click`, `ui-wait-for`, `qml-get`, and whole-window grabs are all listed as available in the Lanista ledger.

**Completion criterion:** only `Runtime-validated` when the committed test build passes deterministic gates, the available-state replay completes in an isolated session with the stated probes/grab, the regressions replay, and the production build is reconfigured with `COLOSSEUM_UPDATE_TESTING=OFF` before packaging.

### Slice L2: The latest chronicle remains visible when no action is available

**Purpose:** Prove the same real page remains useful after completion: the latest update story survives in `UpToDate` and does not leave a deceptive action button.

**Dependencies:** Slice L1 and Tasks 4–8.

**Implementation guidance:** The `update-up-to-date` seed mirrors Task 4's cache contract with a verified latest chronicle and no available release. It shares the names from Slice L1; the primary action is disabled or replaced with the approved check-again affordance according to the locked page state.

**Behavior to preserve:** No installer is downloaded or launched by this replay; a stale/corrupt cache never masquerades as an up-to-date result.

**Baseline:** Preserve the pre-implementation missing-page result separately from Slice L1; do not fabricate a before image after the page lands.

**Focused tests:**

- **Qt Test:** `update_service_harness` exercises persisted chronicle loading and rejects an invalid signature.
- **Qt Quick Test:** `tst_update_page.qml` asserts `UpToDate` retains highlights and its action mapping.
- **Existing harnesses:** `tests/test_update_lanista.ps1` runs the second scenario plus the taskbar/fullscreen regression gates.
- **Negative control:** delete the signed chronicle fixture asset or corrupt its signature and prove the page lands in the recoverable state rather than `UpToDate`.

**Test seam status:** migration required until Task 8's runner/fixtures are registered; available afterward.

**Lanista actions:** `lanista session run tests/lanista_scenarios/update_up_to_date.json --exe native/build-msvc/colosseum.exe --tag updater-current --drive --seed tests/lanista_fixtures/update-up-to-date --verbose`; wait for `bootSplash.visible == false`, click `colosseumUpdateTaskbarButton`, wait for `colosseumUpdatePage.visible == true`, then read `automationState`, the primary action's enabled/text properties, and `colosseumUpdateHighlights.visible`; take a whole-window grab.

**Completion signal:** `colosseumUpdatePage.automationState == "UpToDate"` and `colosseumUpdateHighlights.visible == true`.

**State / events / probes:** `qml-get` asserts `UpToDate`, the expected primary-action state, and visible highlights. The only event allowed is an optional `log-mark` correlation; no invented lifecycle/update event wait.

**Visual evidence:** the isolated-session window grab shows the latest illustrated chronicle and the no-update state together.

**Regression paths:** open Update from every normal taskbar page and return; relaunch a second tagged session using the same seed and prove it produces the same state without contacting GitHub.

**Evidence artifacts:** `artifacts/lanista-sessions/<id>/` for the UpToDate run and `tests/test_update_lanista.ps1` output.

**Bridge status:** available.

**Completion criterion:** `Runtime-validated` only after the UpToDate session passes, the negative control is recorded RED then restored, and Task 11 captures Hemanth's aesthetic verdict from the real release candidate.

---

### Task 1: Establish one canonical app version and prove the data boundary

**Files:**
- Create: `native/update/UpdateVersion.h`
- Create: `native/update/UpdateVersion.cpp`
- Create: `tests/update_version_harness.cpp`
- Create: `tests/test_update_data_boundary.ps1`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/colosseum-test-verification.md`

**Interfaces:**
- Produces: `Colosseum::Update::Version::parseCanonical(QStringView)`, `parseTag(QStringView)`, `canonical()`, `display()`, and `compare(const Version&)`.
- Produces: runtime version through `QCoreApplication::applicationVersion()` and compile definition `COLOSSEUM_VERSION`.
- Consumes: no prior task.

- [ ] **Step 1: Add the failing version harness and CMake target**

```cpp
// tests/update_version_harness.cpp
#include "update/UpdateVersion.h"
#include <cstdlib>
#include <iostream>

using Colosseum::Update::Version;

static void require(bool ok, const char* message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main()
{
    const auto release = Version::parseCanonical(QStringView(u"1.1.0"));
    require(release.has_value(), "1.1.0 parses");
    require(release->canonical() == QStringLiteral("1.1.0"), "canonical keeps three parts");
    require(release->display() == QStringLiteral("1.1"), "display omits patch zero");
    require(Version::parseTag(QStringView(u"v2.4.7")).has_value(), "canonical tag parses");
    require(!Version::parseCanonical(QStringView(u"1.1")).has_value(), "two-part version rejected");
    require(!Version::parseTag(QStringView(u"1.1.0")).has_value(), "tag requires v");
    require(!Version::parseCanonical(QStringView(u"01.1.0")).has_value(), "leading zero rejected");
    require(Version::parseCanonical(QStringView(u"1.2.0"))->compare(*release) > 0,
            "newer minor compares greater");
    std::cout << "UPDATE_VERSION_OK\n";
}
```

Add `update_version_harness` to `native/CMakeLists.txt`, link `Qt6::Core`, register it as `unit` in `tests/CMakeLists.txt`, and add its exact contract to the verification ledger.

- [ ] **Step 2: Build and run the harness to verify RED**

Run: `native\build-target.bat update_version_harness`

Expected: build fails because `update/UpdateVersion.h` and its methods do not exist.

- [ ] **Step 3: Implement the strict version value and one compiled version source**

```cpp
// native/update/UpdateVersion.h
#pragma once
#include <QString>
#include <QStringView>
#include <optional>

namespace Colosseum::Update {
struct Version final {
    int major = 0;
    int minor = 0;
    int patch = 0;
    static std::optional<Version> parseCanonical(QStringView text);
    static std::optional<Version> parseTag(QStringView text);
    QString canonical() const;
    QString display() const;
    int compare(const Version& other) const;
};
}
```

Parse exactly three non-negative base-10 components, reject signs, whitespace, suffixes, empty
components, overflow, and leading zeroes except the component `0`. Set
`project(colosseum VERSION 1.1.0 LANGUAGES C CXX RC)`, compile
`COLOSSEUM_VERSION="${PROJECT_VERSION}"`, and call
`app.setApplicationVersion(QStringLiteral(COLOSSEUM_VERSION))` immediately after setting the app
name.

- [ ] **Step 4: Add and run the data-boundary guard**

`tests/test_update_data_boundary.ps1` must scan production C++ for writable paths derived from
`applicationDirPath()` or the current working directory. Allow read-only runtime/catalog lookups
only; fail writes, `QSettings` files, download roots, ledgers, and caches outside
`QStandardPaths::AppDataLocation`, `CacheLocation`, or injected test roots.

Run: `powershell -ExecutionPolicy Bypass -File tests\test_update_data_boundary.ps1`

Expected: PASS with a printed inventory of read-only install-relative lookups and zero writable
program-tree findings. If it finds a real writable 1.0-era path, move that path behind a tested
AppData migration in this task before proceeding.

- [ ] **Step 5: Verify GREEN and commit**

Run:

```powershell
native\build-target.bat update_version_harness
ctest --test-dir native/build-msvc -R colosseum.update_version_harness --output-on-failure
powershell -ExecutionPolicy Bypass -File tests\test_update_data_boundary.ps1
```

Expected: `UPDATE_VERSION_OK`, CTest pass, and data-boundary pass.

Commit only the listed files with message `feat(updater): establish canonical app version`, then
push `origin master`.

---

### Task 2: Parse inert manifests and verify exact signed bytes

**Files:**
- Create: `native/update/UpdateManifest.h`
- Create: `native/update/UpdateManifest.cpp`
- Create: `native/update/UpdateTrust.h`
- Create: `native/update/UpdateTrust.cpp`
- Create: `native/update/UpdatePublicKey.h`
- Create: `native/update/UpdateTestPublicKey.h`
- Create: `tests/update_manifest_trust_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/colosseum-test-verification.md`

**Interfaces:**
- Consumes: `Version` from Task 1.
- Produces: `verifyEd25519Raw(...)`, `sha256(...)`, `parseManifest(...)`, `Manifest`, `Highlight`, and `Artwork`.

- [ ] **Step 1: Write the failing trust/manifest harness**

Use RFC 8032 Ed25519 test vector 1 directly in the harness: public key
`d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a`, empty message, and signature
`e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b`.

Assert that the original bytes verify, a one-byte mutation fails, invalid key/signature lengths
fail, valid schema-1 JSON parses only after verification, and these manifest inputs are rejected:
two-part version, tag/version mismatch, non-HTTPS production URL, unknown highlight kind,
`javascript`/`qml`/`html` keys, artwork containing a slash, duplicate asset names, non-hex hashes,
and a GitHub notes URL for another repository.

- [ ] **Step 2: Build to verify RED**

Run: `native\build-target.bat update_manifest_trust_harness`

Expected: build fails on missing `UpdateManifest` and `UpdateTrust` interfaces.

- [ ] **Step 3: Generate the production trust root outside the repository**

Run once under the Suprabha account, stopping rather than overwriting if a key already exists:

```powershell
$releaseKeyDir = Join-Path $env:USERPROFILE 'Documents\Colosseum Release Keys'
$privateKeyPath = Join-Path $releaseKeyDir 'update-ed25519.pem'
$publicKeyPath = Join-Path $releaseKeyDir 'update-ed25519-public.der'
if (Test-Path -LiteralPath $privateKeyPath) { throw "Refusing to overwrite existing release key: $privateKeyPath" }
New-Item -ItemType Directory -Force -Path $releaseKeyDir | Out-Null
& openssl genpkey -algorithm ED25519 -out $privateKeyPath
if ($LASTEXITCODE -ne 0) { throw 'Ed25519 private-key generation failed' }
& openssl pkey -in $privateKeyPath -pubout -outform DER -out $publicKeyPath
if ($LASTEXITCODE -ne 0) { throw 'Ed25519 public-key export failed' }
& icacls.exe $privateKeyPath /inheritance:r /grant:r "$env:USERNAME`:(R,W)"
if ($LASTEXITCODE -ne 0) { throw 'Private-key ACL hardening failed' }
$publicDer = [IO.File]::ReadAllBytes($publicKeyPath)
$expectedPrefix = [Convert]::FromHexString('302A300506032B6570032100')
if ($publicDer.Length -ne 44) { throw "Unexpected Ed25519 SPKI length: $($publicDer.Length)" }
for ($i = 0; $i -lt $expectedPrefix.Length; $i++) {
    if ($publicDer[$i] -ne $expectedPrefix[$i]) { throw 'Unexpected Ed25519 SPKI prefix' }
}
($publicDer[12..43] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ', '
```

Use `apply_patch` to place the printed 32 public bytes into `UpdatePublicKey.h`. Never copy the PEM
or DER file into the repository. Record the public-key SHA-256 in a comment for operator comparison.
Create a separate `UpdateTestPublicKey.h` with only the 32 public bytes for the committed Lanista
fixture signatures. Its matching test private key remains outside the repository. CMake selects this
header only for `COLOSSEUM_UPDATE_TESTING=ON`; a normal/release build must compile only against the
production key.

- [ ] **Step 4: Implement the immutable data model and trust seam**

```cpp
namespace Colosseum::Update {
enum class HighlightKind { Feature, Statistic, BeforeAfter, Milestone };
struct Artwork { QString assetName; QByteArray sha256; };
struct Highlight {
    HighlightKind kind;
    QString section, title, body, value, context, beforeCaption, afterCaption;
    QStringList artworkAssets;
};
struct Manifest {
    int schemaVersion = 0;
    Version version;
    QString tag, eyebrow, title, summary, installerAsset, notesUrl;
    qint64 installerSize = 0;
    QByteArray installerSha256;
    Version minimumUpdaterVersion;
    QList<Highlight> highlights;
    QList<Artwork> artwork;
};
std::optional<Manifest> parseManifest(const QByteArray& verifiedUtf8, QString* error);
bool verifyEd25519Raw(QByteArrayView message, QByteArrayView signature,
                      QByteArrayView publicKey, QString* error);
QByteArray sha256(QIODevice* device, QString* error);
}
```

Use OpenSSL EVP Ed25519 without accepting PEM or algorithm metadata from the release. Parsing must
allow only the schema's named keys and enforce bounded text/card counts and lengths before creating
QVariants later. `UpdatePublicKey.h` contains exactly one 32-byte production public key constant;
`UpdateTestPublicKey.h` can be selected only by the explicit test-build CMake definition. Add a
test assertion that the normal app target cannot compile with the test-key definition accidentally
set by packaging.
Add a focused `colosseum_update_crypto` interface target: link `OpenSSL::Crypto` when the imported
target exists, otherwise reuse `${OPENSSL_MSVC_ROOT}/include` and
`${OPENSSL_MSVC_ROOT}/lib/libcrypto.lib`. Link the app and trust harness through that target rather
than pulling in libtorrent.

- [ ] **Step 5: Verify GREEN and the negative control**

Run the harness, then temporarily flip one byte of the RFC signature in the positive case and prove
the harness fails before restoring it.

Run:

```powershell
native\build-target.bat update_manifest_trust_harness
ctest --test-dir native/build-msvc -R colosseum.update_manifest_trust_harness --output-on-failure
```

Expected: `UPDATE_MANIFEST_TRUST_OK` after restoration.

- [ ] **Step 6: Commit and push**

Commit the listed files as `feat(updater): verify signed release manifests`, verify no private-key
material is staged with `git diff --cached --name-only`, then push `origin master`.

---

### Task 3: Discover only valid stable GitHub releases

**Files:**
- Create: `native/update/UpdateReleaseClient.h`
- Create: `native/update/UpdateReleaseClient.cpp`
- Create: `tests/update_release_client_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/colosseum-test-verification.md`

**Interfaces:**
- Consumes: `Manifest`, `verifyEd25519Raw`, and the production public key from Task 2.
- Produces: `ReleaseCheckResult` and asynchronous `UpdateReleaseClient::checkLatest(...)`.

- [ ] **Step 1: Write a failing loopback release-client harness**

Create a `QTcpServer` fixture that serves `/repos/kingoftheseas56/Colosseum/releases/latest`, the
manifest, signature, installer metadata, and artwork metadata. Record request headers. Cover:

- valid stable release with exact three assets and ETag;
- `304 Not Modified` using `If-None-Match`;
- missing manifest/signature/installer;
- duplicate exact installer name;
- draft/prerelease flags even though the latest endpoint should exclude them;
- manifest tag/version/API tag mismatch;
- API digest versus signed digest mismatch;
- unsafe redirect, cross-repository notes URL, oversized API/manifest/signature body;
- HTTP error, timeout, malformed JSON, and cancelled client destruction.

- [ ] **Step 2: Build and run to verify RED**

Run: `native\build-target.bat update_release_client_harness`

Expected: build fails because `UpdateReleaseClient` does not exist.

- [ ] **Step 3: Implement the injectable release client**

```cpp
namespace Colosseum::Update {
struct ReleaseCheckResult {
    enum class Status { Valid, NotModified, Rejected, NetworkError } status;
    Manifest manifest;
    QHash<QString, QUrl> assetUrls;
    QString etag;
    QString errorCode;
};
struct ReleaseClientConfig {
    QUrl latestReleaseUrl;
    QString repository = QStringLiteral("kingoftheseas56/Colosseum");
    QByteArray publicKey;
    bool allowHttpForTests = false;
};
class UpdateReleaseClient final : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(ReleaseCheckResult)>;
    UpdateReleaseClient(QNetworkAccessManager* nam, ReleaseClientConfig config,
                        QObject* parent = nullptr);
    void checkLatest(const QString& priorEtag, Callback done);
    void cancel();
};
}
```

Set GitHub's JSON accept header, API-version header, and `Colosseum/<installed-version>` user agent.
Cap every metadata response, use `NoLessSafeRedirectPolicy`, and resolve exact asset names from the
same release API object. Verify the raw manifest signature before parsing or trusting any signed
field. Treat rejection as no available update, not as availability with an error badge.

- [ ] **Step 4: Verify GREEN**

Run:

```powershell
native\build-target.bat update_release_client_harness
ctest --test-dir native/build-msvc -R colosseum.update_release_client_harness --output-on-failure
```

Expected: `UPDATE_RELEASE_CLIENT_OK`; fixture confirms `If-None-Match` on the second request.

- [ ] **Step 5: Commit and push**

Commit as `feat(updater): discover trusted GitHub releases`, then push `origin master`.

---

### Task 4: Add a bounded cache and resumable installer transport

**Files:**
- Create: `native/update/UpdateCache.h`
- Create: `native/update/UpdateCache.cpp`
- Create: `native/update/UpdateDownload.h`
- Create: `native/update/UpdateDownload.cpp`
- Create: `tests/update_download_harness.cpp`
- Create: `tests/lanista_fixtures/update-available/`
- Create: `tests/lanista_fixtures/update-up-to-date/`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/colosseum-test-verification.md`

**Interfaces:**
- Consumes: signed installer size/hash and asset URL from Tasks 2-3.
- Produces: `UpdateCache`, `DownloadRequest`, and `UpdateDownload` progress/completion signals.

- [ ] **Step 1: Write the failing cache/download harness**

Drive a loopback server with a 2 MiB deterministic byte pattern. Assert fresh streaming, cancel at
600 KiB, restart from the persisted byte count with `Range` and `If-Range`, server-ignored range
restart, changed ETag restart, truncated response failure, wrong length failure, wrong SHA-256
failure, unsafe output path rejection, insufficient-space preflight, successful atomic promotion
from `.part` to `.exe`, and superseded-version cleanup limited to the injected cache root.

- [ ] **Step 2: Build to verify RED**

Run: `native\build-target.bat update_download_harness`

Expected: missing `UpdateCache`/`UpdateDownload` compile failure.

- [ ] **Step 3: Implement cache and transport contracts**

```cpp
struct DownloadRequest {
    Version version;
    QUrl url;
    QString assetName;
    qint64 expectedSize = 0;
    QByteArray expectedSha256;
    QString expectedEtag;
};

class UpdateDownload final : public QObject {
    Q_OBJECT
public:
    UpdateDownload(QNetworkAccessManager* nam, UpdateCache* cache, QObject* parent = nullptr);
    void start(const DownloadRequest& request);
    void cancel();
signals:
    void progress(qint64 received, qint64 total, qint64 bytesPerSecond);
    void completed(QString verifiedInstallerPath);
    void failed(QString errorCode, bool resumable);
};
```

`UpdateCache` derives production root from
`QStandardPaths::writableLocation(AppDataLocation) + "/updates"`; tests inject a `QTemporaryDir`.
Persist metadata with `QSaveFile`, stream bytes through `readyRead`, hash the completed file off the
GUI thread, and compare size and digest before renaming. Validate canonical paths before cleanup.
Define the cache's on-disk state/document names here, then create both Lanista seed directories in
that exact layout. Each seed contains only signed manifest/presentation/cache metadata and optional
local artwork â€” never an installer executable. The Available seed carries version `1.1.1`; the
UpToDate seed carries the verified latest chronicle. Both signatures verify only with the Task 2
test public key in a `COLOSSEUM_UPDATE_TESTING=ON` build.

- [ ] **Step 4: Verify GREEN plus corruption negative control**

Run the harness, mutate the expected digest positive case to prove it fails, restore, and rerun.

```powershell
native\build-target.bat update_download_harness
ctest --test-dir native/build-msvc -R colosseum.update_download_harness --output-on-failure
```

Expected: `UPDATE_DOWNLOAD_OK`.

- [ ] **Step 5: Commit and push**

Commit as `feat(updater): stream and resume verified installers`, then push `origin master`.

---

### Task 5: Implement the typed UpdateService state machine

**Files:**
- Create: `native/update/UpdateService.h`
- Create: `native/update/UpdateService.cpp`
- Create: `tests/update_service_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/colosseum-test-verification.md`

**Interfaces:**
- Consumes: `Version`, `UpdateReleaseClient`, `UpdateCache`, and `UpdateDownload`.
- Produces: QML context contract named `Updates`.

- [ ] **Step 1: Write the failing service harness**

Use injected fake clock, release client, downloader, install-launch callback, cache root, and installed version. Assert exact
transitions for first check, older/equal/newer versions, rejected release, quiet network failure,
six-hour throttle, manual bypass, available/unseen, `markSeen()` clearing only unseen, download,
cancel/resume, verify, ready, persisted chronicle, offline restart, superseding release, failed
target suppression, minimum-updater/manual-path state, verified artwork caching, corrupt/missing
optional artwork falling back without losing the release, and Ready calling the injected launcher.
Add the two Task 4 seed directories as read-only cache-load cases: in the test-key configuration,
Available and UpToDate are reconstructed without a network request; a one-byte signature mutation
must not reach either visible state.

- [ ] **Step 2: Build to verify RED**

Run: `native\build-target.bat update_service_harness`

Expected: missing service interface failure.

- [ ] **Step 3: Implement the QML-facing contract**

```cpp
class UpdateService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString installedVersion READ installedVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool unseenUpdate READ unseenUpdate NOTIFY changed)
    Q_PROPERTY(qint64 receivedBytes READ receivedBytes NOTIFY changed)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QVariantMap release READ release NOTIFY changed)
    Q_PROPERTY(QVariantList highlights READ highlights NOTIFY changed)
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Paused,
                       Verifying, Ready, Installing, RecoverableError,
                       VerificationFailure, ManualUpdateRequired };
    Q_ENUM(State)
    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void markSeen();
    Q_INVOKABLE void restartAndUpdate();
    void startAutomaticChecks();
signals:
    void changed();
};
```

Keep the release chronicle separate from transient error state so offline/check failures never
erase valid copy. Persist last-check, ETag, seen version, cached signed manifest, failed target, and
download metadata. Fetch optional artwork through the dedicated update NAM with an 8 MiB per-asset
cap; verify its signed SHA-256 before `UpdateCache` atomically promotes it, and expose local file URLs
only. Convert native structs to bounded inert `QVariantMap`/`QVariantList` only here. The testable
constructor accepts
`std::function<bool(const QString&, const Version&, QString*)> installLauncher`; production supplies
`UpdateInstallBridge` in Task 8.

- [ ] **Step 4: Verify GREEN**

Run:

```powershell
native\build-target.bat update_service_harness
ctest --test-dir native/build-msvc -R colosseum.update_service_harness --output-on-failure
```

Expected: `UPDATE_SERVICE_OK`.

- [ ] **Step 5: Commit and push**

Commit as `feat(updater): coordinate update lifecycle`, then push `origin master`.

---

### Task 6: Add the permanent taskbar Update icon and shell route

**Files:**
- Create: `assets/icons/update.svg`
- Create: `tests/test_update_taskbar_p0.ps1`
- Modify: `qml/Taskbar.qml`
- Modify: `qml/Main.qml`

**Interfaces:**
- Consumes: `Updates.updateAvailable`, `Updates.unseenUpdate`, and `Updates.state` from Task 5.
- Produces: `Taskbar.updateClicked()`, `updateActive`, `updateAvailable`, `updateUnseen`, and an `updateLayer` loader.

- [ ] **Step 1: Write the failing static/contract gate**

`tests/test_update_taskbar_p0.ps1` must assert the SVG exists with explicit stroke/fill rather than
`currentColor`; Taskbar declares all four update properties/signals; the icon is visible whenever
the expanded bar is visible; availability has a persistent gold badge; unseen state alone drives a
pulse; active state has the standard underline; the accessible name includes "Update available";
and `Main.qml` binds the live service and opens a mutually exclusive loader.

- [ ] **Step 2: Run to verify RED**

Run: `powershell -ExecutionPolicy Bypass -File tests\test_update_taskbar_p0.ps1`

Expected: fails on missing icon and missing update contract.

- [ ] **Step 3: Implement the taskbar item and shell route**

Add the Update item beside Settings using the existing 46x46/13-radius vocabulary:

```qml
signal updateClicked()
property bool updateActive: false
property bool updateAvailable: false
property bool updateUnseen: false
```

The clickable item must be `objectName: "colosseumUpdateTaskbarButton"`; the availability dot is
`colosseumUpdateBadge`. They are stable automation surfaces, never QML-only ids. This task remains
`Test-reported` until Task 8 replays the real shell route through Slice L1.

The badge is a 12px gold circle visible for `updateAvailable`; its `SequentialAnimation` runs only
for `updateUnseen`. Add `openUpdatePage()`/`closeUpdatePage()` in `Main.qml`; opening any taskbar
full page closes Downloads, Extensions, Settings, and Update before activating exactly one. Opening
Update calls `Updates.markSeen()`.

- [ ] **Step 4: Verify GREEN with the focused and existing taskbar gates**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_update_taskbar_p0.ps1
powershell -ExecutionPolicy Bypass -File tests\test_taskbar_session_icons_p0.ps1
powershell -ExecutionPolicy Bypass -File tests\test_taskbar_download_reveal_p0.ps1
```

Expected: all pass; existing session/download taskbar behavior is unchanged.

- [ ] **Step 5: Commit and push**

Commit as `feat(updater): add taskbar update notification`, then push `origin master`.

---

### Task 7: Build the approved Update page and release chronicle

**Files:**
- Create: `qml/UpdatePage.qml`
- Create: `qml/update/UpdateReleaseHero.qml`
- Create: `qml/update/UpdateHighlightCard.qml`
- Create: `tests/qml/tst_update_page.qml`
- Create: `tests/lanista_scenarios/update_available.json`
- Create: `tests/lanista_scenarios/update_up_to_date.json`
- Modify: `qml/Main.qml`

**Interfaces:**
- Consumes: the exact `UpdateService` properties/actions from Task 5.
- Produces: full-page Update UI with object names `colosseumUpdatePage`, `colosseumUpdatePrimaryAction`, `colosseumUpdateProgress`, `colosseumUpdateHighlights`, and `colosseumUpdateStatusText`; the page exposes read-only `automationState` and `automationVersion` bound to the actual service.

- [ ] **Step 1: Write the failing Qt Quick Test**

Create a fake `QtObject` with the exact service properties and methods. Instantiate `UpdatePage`
inside a real `Window` and data-drive every state. Assert primary copy/action, click dispatch,
progress text/bar, cancel/resume, no action during Checking/Verifying, Ready invoking
`restartAndUpdate`, latest highlights remaining present in UpToDate, missing artwork fallback, long
copy wrapping, narrow width, keyboard focus, Escape/back, and reduced-motion property disabling the
pulse/animated progress treatment without hiding status.

- [ ] **Step 2: Build/run Quick Test to verify RED**

Run:

```powershell
native\build-target.bat colosseum_qml_tests
ctest --test-dir native/build-msvc -R colosseum.qml --output-on-failure
```

Expected: QML test fails because `UpdatePage.qml` is absent.

- [ ] **Step 3: Implement the approved page from local components**

`UpdatePage.qml` follows the locked concept: black/glass full-page shell, gold eyebrow, large serif
version title, editorial summary, left release/download hero, right highlight cards, persistent
chronicle after UpToDate, and standard back/minimize/fullscreen/power chrome. Map only the four
whitelisted highlight kinds to local templates; unknown kinds render no card. Use the service's
human-readable state and never expose raw logs as page copy.

Primary actions map exactly:

```qml
function invokePrimaryAction() {
    if (updates.state === updates.Available) { updates.download(); return }
    if (updates.state === updates.Paused) { updates.download(); return }
    if (updates.state === updates.Ready) { updates.restartAndUpdate(); return }
    if (updates.state === updates.RecoverableError) { updates.download(); return }
    updates.checkNow()
}
```

The test must exercise the actual click and prove the service method ran with its receiver intact.
Create the two static Lanista scenarios now, but do not run them until Task 8 has exposed the real
`Updates` service in an isolated app process. `update_available.json` waits for
`bootSplash.visible == false`, clicks `colosseumUpdateTaskbarButton`, waits for
`colosseumUpdatePage.visible == true`, and reads the Available properties described in Slice L1.
`update_up_to_date.json` follows the same route and reads the UpToDate properties described in
Slice L2. Both use namespaced `objectName` targets only, explicit `timeout_ms`, whole-window grabs,
and no download/install click.

- [ ] **Step 4: Verify QML GREEN and loadability**

Run:

```powershell
native\build-target.bat colosseum_qml_tests
ctest --test-dir native/build-msvc -R colosseum.qml --output-on-failure
powershell -ExecutionPolicy Bypass -File tests\test_fullscreen_controls_p0.ps1
```

Expected: all QML tests pass and Update page carries standard fullscreen chrome.

- [ ] **Step 5: Commit and push**

Commit as `feat(updater): add illustrated update page`, then push `origin master`.

---

### Task 8: Wire production checks without touching first paint

**Files:**
- Modify: `native/main.cpp`
- Modify: `native/CMakeLists.txt`
- Create: `tests/update_install_bridge_harness.cpp`
- Create: `native/update/UpdateInstallBridge.h`
- Create: `native/update/UpdateInstallBridge.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/colosseum-test-verification.md`
- Create: `tests/test_update_lanista.ps1`
- Modify: `docs/colosseum-lanista-verification.md`

**Interfaces:**
- Consumes: `UpdateService` and `COLOSSEUM_VERSION`.
- Produces: production `Updates` context property, installed/dev eligibility, startup result parsing, and `restartAndUpdate()` handoff.

- [ ] **Step 1: Write the failing bridge harness**

Test installed-layout recognition, source-tree/dev suppression, registry/expected-install-root
eligibility, verified installer required inside the update cache, detached argument construction,
startup parsing for success/rollback/failure, exact sibling backup-path validation, and refusal to
clean any path outside `%LOCALAPPDATA%\Programs\Colosseum.__update-old` in an injected temporary
layout.

- [ ] **Step 2: Build to verify RED**

Run: `native\build-target.bat update_install_bridge_harness`

Expected: missing bridge interface failure.

- [ ] **Step 3: Implement bridge and production construction**

```cpp
struct InstallLaunch {
    QString program;
    QStringList arguments;
    QString workingDirectory;
};

class UpdateInstallBridge final : public QObject {
    Q_OBJECT
public:
    bool installedBuildEligible() const;
    std::optional<InstallLaunch> prepare(const QString& verifiedInstaller,
                                         const Version& target, QString* error) const;
    bool launchDetached(const InstallLaunch& launch, QString* error) const;
    void acknowledgeHealthyBoot(const QStringList& arguments);
};
```

In `main.cpp`, create a dedicated update NAM and service after application identity is set, expose
it as `Updates`, load QML, then invoke `startAutomaticChecks()` only after `rootObjects()` is
non-empty. Disable automatic checks when the executable resolves inside a Git checkout or
`COLOSSEUM_DEV` is set; allow explicit test opt-in only through injected configuration, not a public
release environment variable. Add the test-build CMake option `COLOSSEUM_UPDATE_TESTING` (default
OFF) that selects only the Task 2 public test key, permits the signed Task 4 cache seeds to load,
and is compiled out of packaging: `package_release.sh` must inspect `CMakeCache.txt` and refuse
ON. It does not enable download, installer launch, or a production GitHub override. Add
`tests/test_update_lanista.ps1`: it builds the explicit test configuration in the approved build
directory, runs both `lanista session run` scenarios with `--drive`, `--seed`, `--tag`, and
`--verbose`, preserves their session paths, then uses a PowerShell `finally` block to restore
`COLOSSEUM_UPDATE_TESTING=OFF` and rebuild `colosseum` before exiting. Update both ledgers in the
same commit with the runner, object names, scenario commands, isolation guarantee, and status.

- [ ] **Step 4: Build and verify GREEN**

Run:

```powershell
native\build-target.bat update_install_bridge_harness
native\build-target.bat colosseum
ctest --test-dir native/build-msvc -R "colosseum.update_(install_bridge|service)_harness" --output-on-failure
powershell -ExecutionPolicy Bypass -File tests\test_update_lanista.ps1
```

Expected: bridge/service tests pass; both isolated Lanista sessions create manifests under
`artifacts/lanista-sessions/`, report the Available and UpToDate state respectively, and the final
restored build has `COLOSSEUM_UPDATE_TESTING=OFF`. Record the two session paths in the task handoff.

- [ ] **Step 5: Commit and push**

Commit as `feat(updater): wire installed-build update service`, then push `origin master`.

---

### Task 9: Extend NSIS with side-by-side update and rollback

**Files:**
- Modify: `scripts/installer/colosseum.nsi`
- Modify: `scripts/installer/package_release.sh`
- Create: `tests/installer/update_matrix.ps1`

**Interfaces:**
- Consumes: installer launch arguments from `UpdateInstallBridge`.
- Produces: `/UPDATE=1 /WAITPID=<pid> /TARGETVERSION=X.Y.Z /RESTART=1 /LOG=<path>` contract and startup result arguments.

- [ ] **Step 1: Write the failing isolated installer matrix**

The matrix builds tiny fixture payloads with sentinel executables/files rather than the 253 MB app.
Use a temporary fake `%LOCALAPPDATA%` and explicit NSIS test defines. Assert normal install,
successful N→N+1 swap, exact PID wait, shortcut/registry version refresh, extraction sentinel
failure leaving N untouched, rename failure restoring N, N+1 relaunch arguments, old backup retained,
and update mode never executing the uninstall section.

- [ ] **Step 2: Run to verify RED**

Run: `powershell -ExecutionPolicy Bypass -File tests\installer\update_matrix.ps1`

Expected: fails because `colosseum.nsi` has no update-mode contract.

- [ ] **Step 3: Implement NSIS update mode**

Parse named parameters with `FileFunc.nsh`. Wait for the exact process using Win32 handles:

```nsis
System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i $WaitPid) p .r0'
${If} $0 P<> 0
  System::Call 'kernel32::WaitForSingleObject(p r0, i 120000) i .r1'
  System::Call 'kernel32::CloseHandle(p r0)'
${EndIf}
```

Use fixed sibling directories `Colosseum.__update-new` and `Colosseum.__update-old`; reject any
pre-existing unexpected reparse point; extract and validate
`native\build-msvc\colosseum.exe` before rename; restore the old directory on any swap failure;
write an update log outside the payload; relaunch N+1 with
`--update-result=success --update-from=<old> --update-backup=<exact sibling>` or relaunch N with
`--update-result=rollback --update-target=<new>`.

Normal interactive installation remains unchanged. Update mode is silent because consent already
happened on the Update page.

- [ ] **Step 4: Tighten packaging inputs**

`package_release.sh X.Y.Z` must reject non-canonical versions, dirty source trees, tag mismatch,
missing clean `BUILD_DIR`, `COLOSSEUM_UPDATE_TESTING=ON` in the build cache, missing runtime
sentinels, and installer filename drift. It must print the installer path, byte size, and SHA-256 as
machine-readable final lines for Task 10.

- [ ] **Step 5: Verify GREEN with the matrix and real package smoke**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\installer\update_matrix.ps1
& 'C:\Program Files\Git\bin\bash.exe' scripts/installer/package_release.sh 1.1.0
```

Expected: isolated matrix passes; real package emits `Colosseum-1.1.0-setup.exe`, size, and SHA-256.
Do not publish it in this task.

- [ ] **Step 6: Commit and push**

Commit as `feat(installer): add rollback-safe update mode`, then push `origin master`.

---

### Task 10: Generate signed manifests and publish draft releases safely

**Files:**
- Create: `scripts/update/generate_update_manifest.py`
- Create: `scripts/update/verify_update_release.py`
- Create: `scripts/update/update-manifest-v1.schema.json`
- Create: `tests/update_release_tooling_test.py`
- Create: `release/presentation/1.1.0.json`
- Modify: `scripts/publish_app_release.py`

**Interfaces:**
- Consumes: packaged installer output from Task 9 and a release-presentation JSON input.
- Produces: exact `colosseum-update-v1.json`, `.sig`, installer/artwork upload set, and verified draft release.

- [ ] **Step 1: Write failing Python tooling tests**

Use `unittest`, `tempfile`, and a fake GitHub transport. Prove deterministic byte-for-byte manifest
generation; strict `X.Y.Z`/`vX.Y.Z`; installer/artwork size and SHA-256; stable key ordering and UTF-8
without BOM; Ed25519 signature round-trip through `openssl pkeyutl`; rejection of executable keys,
path-bearing artwork names, missing artwork, duplicate assets, wrong repo notes URL, dirty/tag-mismatch
release input; draft creation; exact MIME types; uploaded digest verification; and refusal to replace
or mutate an already-published release.

- [ ] **Step 2: Run to verify RED**

Run: `python tests\update_release_tooling_test.py`

Expected: import/file failure because update tooling does not exist.

- [ ] **Step 3: Implement deterministic generation and draft verification**

The generator CLI is exact:

```text
python scripts/update/generate_update_manifest.py \
  --version 1.1.0 \
  --installer dist/Colosseum-1.1.0-setup.exe \
  --presentation release/presentation/1.1.0.json \
  --private-key "C:\Users\Suprabha\Documents\Colosseum Release Keys\update-ed25519.pem" \
  --output-dir dist/update-1.1.0
```

It emits the manifest and detached raw signature only after schema validation. The publisher accepts
`--draft`, uploads installer/manifest/signature/artwork, re-fetches every asset, compares size and
GitHub digest, invokes `verify_update_release.py`, and leaves the release draft. A separate explicit
`--publish-verified-draft` operation publishes only a draft that passes verification and has the
expected tag commit.

Seed `release/presentation/1.1.0.json` with the approved bootstrap chronicle:

```json
{
  "eyebrow": "A NEW CHAPTER IS READY",
  "title": "Colosseum 1.1",
  "summary": "The house can now tell you when a new release is ready, show what changed, and carry the verified update into place.",
  "notes_url": "https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.0",
  "highlights": [
    {
      "kind": "feature",
      "section": "COLOSSEUM",
      "title": "The house now keeps itself current.",
      "body": "A quiet taskbar signal leads to the new Update page, where each verified release downloads with progress and remains as the latest chapter afterward."
    }
  ],
  "artwork": []
}
```

Additional cards may be added only for features actually present in the 1.1.0 release candidate.

- [ ] **Step 4: Prove the external signer still matches the embedded public key**

Run the generator in comparison mode before producing any release bytes:

```powershell
$publicKeyPath = Join-Path $env:USERPROFILE 'Documents\Colosseum Release Keys\update-ed25519-public.der'
if (-not (Test-Path -LiteralPath $publicKeyPath)) { throw "Missing release public key: $publicKeyPath" }
python scripts/update/generate_update_manifest.py --public-key-der $publicKeyPath --check-cpp-header native/update/UpdatePublicKey.h
if ($LASTEXITCODE -ne 0) { throw 'External signer does not match embedded update key' }
```

Implement and test `--check-cpp-header` as a read-only comparison: validate the 44-byte Ed25519 SPKI
prefix, extract its final 32 bytes, parse the header's 32 byte literals, and fail on any mismatch.
Verify `git status` never shows either external key file.

- [ ] **Step 5: Verify tooling GREEN**

Run:

```powershell
python tests\update_release_tooling_test.py
native\build-target.bat update_manifest_trust_harness
ctest --test-dir native/build-msvc -R colosseum.update_manifest_trust_harness --output-on-failure
```

Expected: Python suite passes and C++ verifier accepts generator output signed by a temporary test
key while rejecting a one-byte mutation.

- [ ] **Step 6: Commit and push**

Commit code/schema/tests/public-key header only as `feat(release): publish signed update drafts`.
Inspect the cached diff for PEM, DER, or private-key bytes before committing, then push `origin master`.

---

### Task 11: Prove the complete update journey and close release documentation

**Files:**
- Create: `docs/release-notes/v1.1.0.md`
- Modify: `README.md`
- Modify: `docs/colosseum-test-verification.md`
- Modify: implementation files only if a failing gate proves a defect; each fix receives its own focused commit.

**Interfaces:**
- Consumes: all Tasks 1-10.
- Produces: verified previous-release→1.1.0 bootstrap, updater-enabled N→N+1 proof, final test ledger, and user-facing bootstrap instructions.

- [ ] **Step 1: Run every deterministic gate from the committed tree**

Run:

```powershell
native\build-msvc.bat
ctest --test-dir native/build-msvc -L unit --output-on-failure
ctest --test-dir native/build-msvc -R colosseum.qml --output-on-failure
powershell -ExecutionPolicy Bypass -File tests\test_update_taskbar_p0.ps1
powershell -ExecutionPolicy Bypass -File tests\test_update_data_boundary.ps1
powershell -ExecutionPolicy Bypass -File tests\installer\update_matrix.ps1
python tests\update_release_tooling_test.py
powershell -ExecutionPolicy Bypass -File tests\test_update_lanista.ps1
```

Expected: full build and every deterministic gate pass; the Lanista runner preserves two isolated
session manifests and restores the production-key build before returning. Record exact test counts
and both session paths in the verification ledgers.

- [ ] **Step 2: Build and verify a GitHub draft from the committed tag candidate**

Build from an archive/clean sandbox of the exact commit, package 1.1.0, generate signed assets with
the production key, upload a draft, and run `verify_update_release.py` against the downloaded draft
assets. Confirm tag commit, MIME types, sizes, GitHub digests, raw signature, installer digest, and
all artwork hashes. Keep the release draft.

- [ ] **Step 3: Run the Windows bootstrap matrix with representative user state**

On a clean Windows test account/install root:

1. install public Colosseum 1.0;
2. create representative progress/settings and one owned/downloaded item in Tankoban, Biblio, and Theatre;
3. install the updater-enabled 1.1.0 bootstrap manually once;
4. verify every representative item and setting survived;
5. point an injected test configuration at a signed 1.1.1 draft fixture;
6. discover it through the real Update page, download, interrupt, relaunch, resume, verify, and click **Restart and update**;
7. verify 1.1.1 reopens, the old payload remains until shell-ready acknowledgement, then is cleaned;
8. repeat with a deliberately broken new payload and prove rollback reopens 1.1.0 with user state intact.

Capture installer/update logs under the verification artifacts directory and summarize only the
pass/fail evidence in the ledger.

- [ ] **Step 4: Perform eyes-on verification of the approved surface**

Launch the committed production binary. Review the two Lanista whole-window grabs and then confirm
under Hemanth's eyes: quiet gold taskbar badge/pulse, click opens the approved page, feature cards
remain while downloading, progress is readable, Ready becomes **Restart and update**, and UpToDate
preserves the latest illustrated chronicle. Check 1280x720 and the normal display size, keyboard
navigation, and reduced motion. Record Hemanth's approved/rejected/pending aesthetic verdict beside
the Slice L1/L2 session manifests; Lanista proves state and route, never taste.

- [ ] **Step 5: Cross-substrate self-review against the written Definition of Done**

Use `brotherhood-review` on the complete diff and the ten-item Definition of Done in
`docs/superpowers/specs/2026-08-08-colosseum-auto-update-design.md`. Require MET/PARTIAL/NOT-MET for
every item and REQUEST-CHANGES for any P0/P1 gap. Fix each accepted finding with its own tests and
repeat the affected gates.

- [ ] **Step 6: Write bootstrap/release documentation**

`docs/release-notes/v1.1.0.md` must say that 1.0 users perform one final manual install and that
later stable releases arrive in Colosseum. `README.md` names the Update page and preserves the
manual GitHub download fallback. Update the test ledger with the new targets, labels, commands,
matrix result, and exact clean-machine evidence.

- [ ] **Step 7: Final commit, committed-artifact rerun, and push**

Commit documentation as `docs(release): document Colosseum auto-update bootstrap`, push, rebuild
from the committed tree, rerun the full deterministic gate, and verify the executable mtime/version
reports `1.1.0`. Do not publish the GitHub draft until Hemanth explicitly approves the release
after eyes-on verification.

---

## Final handoff checklist

- [ ] Every task commit is on `master`, pushed, and limited to its declared files.
- [ ] No private signing key or unrestricted remote presentation content exists in the repository.
- [ ] The latest stable endpoint remains quiet for draft/prerelease/invalid releases.
- [ ] The committed app reports the same version as tag, manifest, installer, and Apps list.
- [ ] The taskbar badge/pulse and Update page match the approved design.
- [ ] The 253 MB-class path streams, resumes, verifies, and survives interruption without GUI stalls.
- [ ] Normal install still works; update is side-by-side; failure rolls back; uninstall is never called.
- [ ] Public 1.0→bootstrap and updater-enabled N→N+1 preserve representative user state.
- [ ] Unit, QML, installer, release-tooling, full-build, clean-machine, and eyes-on gates are green.
- [ ] Cross-substrate self-review approves every Definition-of-Done item.
