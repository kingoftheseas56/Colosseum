# Felt-Speed Arc — Stage 0: Prove the Poster Pipe — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Instrument the real binary with a poster scoreboard, bundle the WebP decoder properly, pin the wallpaper host — then prove full shelves with a measured run and Hemanth's eyes.

**Architecture:** A `PosterScoreboard` QObject counts every reply finishing on the QML image NAM (`CachingNam` in `native/main.cpp`) into arrived / network-failed / undecodable buckets per host; classification is a pure static function (harness-testable without sockets). The WebP fix is deploy-side (`deploy-runtime.bat` + installer guard). Host pinning is one list entry. Spec: `docs/superpowers/specs/2026-07-24-colosseum-felt-speed-arc-design.md` (Stage 0 is a hard gate — Stages 1–3 get their own plans after this gate passes).

**Tech Stack:** Qt 6.11.1 / MSVC (`native/build-msvc.bat` — invoke by **absolute path**, `cmd //c` relative invocation fails), house harness style (plain `main()` + `CHECK` macro, exes land in `native/build-msvc/`).

---

## ⚠ Repo-state warnings for the executor (read before Task 0)

1. **The index is NOT yours.** A2's ML revert (~92 files, alignment/guided deletion) sits **staged but uncommitted** in the main tree, with related **unstaged** edits to `native/CMakeLists.txt`, `native/main.cpp`, `qml/MangaReader.qml`, `.gitattributes`. Tasks 1–4 edit two of those same files. **Task 0 must land the revert first** — otherwise any `git commit` mixing pathspecs will entangle the two workstreams.
2. **Never bare `git commit`.** Every commit in this plan uses explicit pathspec (`git commit -- <paths>`) and verifies with `git status --short` after. (House rule; doubly critical given warning 1.)
3. **Do not touch `git stash`.** `stash@{0}` holds Agent 0's Top Manga WIP (separate thread). No `stash pop/drop/clear` anywhere in this plan.
4. **One build per out-dir; kill the running exe by PID first** (`tasklist | grep -i colosseum`, then `taskkill //PID <pid> //F`). A running `colosseum.exe` locks its own exe.
5. **Commit + push together** (house rule): every commit step ends with `git push`.
6. Colosseum is its **own nested repo** — `cd` into `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum` before any git command.

---

### Task 0: Land A2's revert (prerequisite gate — needs Hemanth's one-word go)

The revert is Hemanth-directed ("we are reverting all ONNX… agent 2 did it for me") but uncommitted. It is a brother's work: **confirm with Hemanth before committing it** (one question: "A2's revert is staged but uncommitted — commit and push it now as his revert?"). If he says A2 will land it himself, STOP this plan until master contains the revert.

**Files:** commits exactly what A2 staged + his related unstaged edits. Excludes `wallpapers.ini` (runtime noise) and all untracked files.

- [ ] **Step 1: Verify what is staged and unstaged**

Run:
```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum"
git diff --cached --stat | tail -3     # expect: ~92 files, ~14,388 deletions
git diff --stat                        # expect ONLY: .gitattributes, native/CMakeLists.txt, native/main.cpp, qml/MangaReader.qml, wallpapers.ini
```
Expected: matches the two expectations above. If anything else appears, STOP and report.

- [ ] **Step 2: Stage the revert's unstaged edits (NOT wallpapers.ini)**

```bash
git add -- .gitattributes native/CMakeLists.txt native/main.cpp qml/MangaReader.qml
git status --short | grep -v "^??" | grep -v "^A\|^M\|^D " || true
git diff --stat        # expect ONLY wallpapers.ini remaining unstaged
```

- [ ] **Step 3: Commit the revert (bare commit is CORRECT here — the index IS exactly the revert) and push**

```bash
git commit -m "[Agent 2 (Claude), biblio] revert: remove ONNX/whisper/alignment + guided reader (scope-creep cut, Hemanth-directed 2026-07-24)

Felt-speed arc prerequisite: the app sheds the ML subsystems whole.
Swept by Agent 0 with Hemanth's go."
git push
git status --short | grep -v "^??"     # expect ONLY: ' M wallpapers.ini'
```

- [ ] **Step 4: Prove the shed tree still builds**

Kill any running exe by PID, then:
```bash
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\build-msvc.bat"
```
Expected: build succeeds, `colosseum.exe` fresh. If the revert left dangling CMake references, fix ONLY the dangling reference lines in `native/CMakeLists.txt`, amend nothing — commit the fix separately with pathspec `-- native/CMakeLists.txt`, push.

---

### Task 1: PosterScoreboard — pure classify + counters (TDD)

**Files:**
- Create: `native/net/PosterScoreboard.h`
- Create: `native/net/PosterScoreboard.cpp`
- Test: `tests/poster_scoreboard_harness.cpp`
- Modify: `native/CMakeLists.txt` (two anchors: colosseum target source list near line 89-91 `net/LoopbackPinProxy.cpp`; harness blocks near line 352 `pin_proxy_factory_harness`)

- [ ] **Step 1: Write the failing harness**

Create `tests/poster_scoreboard_harness.cpp`:
```cpp
// poster_scoreboard_harness.cpp — proves Stage 0 classification + aggregation:
// every reply lands in exactly one bucket; webp-without-decoder is Undecodable;
// per-host rows aggregate; summaryText is empty until something is recorded.
#include "../native/net/PosterScoreboard.h"
#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do{ if(!(c)){ ++fails; std::printf("FAIL: %s\n", l);} }while(0)

int main() {
    using B = PosterScoreboard::Bucket;

    // classify: pure, no sockets
    CHECK(PosterScoreboard::classify(200, "image/jpeg", false, false) == B::Arrived,
          "200 jpeg -> Arrived");
    CHECK(PosterScoreboard::classify(200, "image/webp", false, false) == B::Undecodable,
          "200 webp, no decoder -> Undecodable");
    CHECK(PosterScoreboard::classify(200, "IMAGE/WEBP; charset=binary", false, false) == B::Undecodable,
          "content-type case/params ignored");
    CHECK(PosterScoreboard::classify(200, "image/webp", false, true) == B::Arrived,
          "200 webp, decoder present -> Arrived");
    CHECK(PosterScoreboard::classify(404, "text/html", false, true) == B::NetworkFailed,
          "404 -> NetworkFailed");
    CHECK(PosterScoreboard::classify(200, "image/jpeg", true, true) == B::NetworkFailed,
          "transport error wins -> NetworkFailed");
    CHECK(PosterScoreboard::classify(0, "", false, true) == B::Arrived,
          "finished, no error, no status (wrapped reply) -> Arrived");

    // record + summary: per-host rows, bytes sum
    PosterScoreboard sb;
    sb.setWebpDecoderPresent(false);
    CHECK(sb.summaryText().isEmpty(), "empty scoreboard -> empty text");
    sb.record("images.metahub.space", 200, "image/jpeg", 1000, false);
    sb.record("images.metahub.space", 200, "image/webp", 2000, false);
    sb.record("images.metahub.space", 404, "", 0, false);
    sb.record("wsrv.nl", 200, "image/jpeg", 500, false);
    const QVariantMap s = sb.summary();
    const QVariantMap metahub = s.value("images.metahub.space").toMap();
    CHECK(metahub.value("arrived").toLongLong() == 1,      "metahub arrived == 1");
    CHECK(metahub.value("undecodable").toLongLong() == 1,  "metahub undecodable == 1");
    CHECK(metahub.value("failed").toLongLong() == 1,       "metahub failed == 1");
    CHECK(metahub.value("bytes").toLongLong() == 3000,     "metahub bytes == 3000");
    CHECK(s.value("wsrv.nl").toMap().value("arrived").toLongLong() == 1, "wsrv arrived == 1");
    CHECK(!sb.summaryText().isEmpty(), "recorded scoreboard -> non-empty text");

    std::printf(fails ? "FAILS: %d\n" : "poster_scoreboard_harness: ALL PASS\n", fails);
    return fails;
}
```

- [ ] **Step 2: Add the harness + sources to CMake (shared file — grep-verify after edit)**

In `native/CMakeLists.txt`, colosseum target source list — directly after the existing lines
```cmake
    net/LoopbackPinProxy.cpp       # instant posters: pin the connection, keep the hostname (HTTP/2)
    net/LoopbackPinProxy.h
    net/PinProxyFactory.h
```
add:
```cmake
    net/PosterScoreboard.cpp       # felt-speed Stage 0: arrived/failed/undecodable per host
    net/PosterScoreboard.h
```
After the `pin_proxy_factory_harness` block (ends `target_link_libraries(pin_proxy_factory_harness PRIVATE Qt6::Core Qt6::Network)`), add:
```cmake
add_executable(poster_scoreboard_harness
    ../tests/poster_scoreboard_harness.cpp
    net/PosterScoreboard.cpp
    net/PosterScoreboard.h
)
target_include_directories(poster_scoreboard_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(poster_scoreboard_harness PRIVATE Qt6::Core)
```
Then verify the shared-file edit took cleanly:
```bash
grep -n "PosterScoreboard" native/CMakeLists.txt   # expect exactly 4 hits (2 target, 2 harness)
```

- [ ] **Step 3: Run the build to verify the harness FAILS (files don't exist yet)**

```bash
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\build-msvc.bat"
```
Expected: FAILS — `PosterScoreboard.h` not found.

- [ ] **Step 4: Write the implementation**

Create `native/net/PosterScoreboard.h`:
```cpp
#pragma once
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Felt-speed arc, Stage 0 (spec 2026-07-24): the poster scoreboard.
// Every reply finishing on the QML image NAM lands in exactly ONE bucket per host:
//   Arrived       — HTTP success and (if webp) decodable on THIS machine
//   NetworkFailed — transport error or HTTP >= 400
//   Undecodable   — arrived as image/webp with no webp decoder present (the dev-hack scar)
// classify() is pure/static so the harness drives it without sockets. record() is
// mutex-guarded: the QML engine creates NAMs on more than one thread and replies
// finish on their own thread (watch() connects without a receiver context).
class PosterScoreboard : public QObject {
    Q_OBJECT
public:
    enum class Bucket { Arrived, NetworkFailed, Undecodable };

    explicit PosterScoreboard(QObject *parent = nullptr) : QObject(parent) {}

    void setWebpDecoderPresent(bool present) { m_webpPresent = present; }
    bool webpDecoderPresent() const { return m_webpPresent; }

    static Bucket classify(int httpStatus, const QString &contentType,
                           bool networkError, bool webpDecoderPresent);

    void record(const QString &host, int httpStatus, const QString &contentType,
                qint64 bytes, bool networkError);

    Q_INVOKABLE QVariantMap summary() const; // { host: {arrived, failed, undecodable, bytes} }
    QString summaryText() const;             // one log block; empty when nothing recorded

private:
    struct Row { qint64 arrived = 0; qint64 failed = 0; qint64 undecodable = 0; qint64 bytes = 0; };
    mutable QMutex m_mutex;
    QHash<QString, Row> m_rows;
    bool m_webpPresent = false;              // set once at boot, before any NAM exists
};
```

Create `native/net/PosterScoreboard.cpp`:
```cpp
#include "PosterScoreboard.h"

PosterScoreboard::Bucket PosterScoreboard::classify(int httpStatus, const QString &contentType,
                                                    bool networkError, bool webpDecoderPresent)
{
    if (networkError || httpStatus >= 400)
        return Bucket::NetworkFailed;
    // A finished reply with no error and no status is a wrapped reply (e.g. GunzipReply's
    // inner before attribute forwarding) — treat as arrived rather than inventing a failure.
    const QString ct = contentType.toLower();
    if (ct.startsWith(QLatin1String("image/webp")) && !webpDecoderPresent)
        return Bucket::Undecodable;
    return Bucket::Arrived;
}

void PosterScoreboard::record(const QString &host, int httpStatus, const QString &contentType,
                              qint64 bytes, bool networkError)
{
    const Bucket b = classify(httpStatus, contentType, networkError, m_webpPresent);
    QMutexLocker lock(&m_mutex);
    Row &row = m_rows[host];
    switch (b) {
    case Bucket::Arrived:       ++row.arrived; break;
    case Bucket::NetworkFailed: ++row.failed; break;
    case Bucket::Undecodable:   ++row.undecodable; break;
    }
    if (bytes > 0)
        row.bytes += bytes;
}

QVariantMap PosterScoreboard::summary() const
{
    QMutexLocker lock(&m_mutex);
    QVariantMap out;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        QVariantMap row;
        row.insert(QStringLiteral("arrived"), it->arrived);
        row.insert(QStringLiteral("failed"), it->failed);
        row.insert(QStringLiteral("undecodable"), it->undecodable);
        row.insert(QStringLiteral("bytes"), it->bytes);
        out.insert(it.key(), row);
    }
    return out;
}

QString PosterScoreboard::summaryText() const
{
    QMutexLocker lock(&m_mutex);
    QString out;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        out += QStringLiteral("  %1  arrived=%2 failed=%3 undecodable=%4 bytes=%5\n")
                   .arg(it.key()).arg(it->arrived).arg(it->failed)
                   .arg(it->undecodable).arg(it->bytes);
    }
    return out;
}
```

- [ ] **Step 5: Build and run the harness — verify ALL PASS**

```bash
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\build-msvc.bat"
./native/build-msvc/poster_scoreboard_harness.exe
```
Expected: `poster_scoreboard_harness: ALL PASS`, exit 0.

- [ ] **Step 6: Commit (explicit pathspec) and push**

```bash
git add -- native/net/PosterScoreboard.h native/net/PosterScoreboard.cpp tests/poster_scoreboard_harness.cpp native/CMakeLists.txt
git diff --cached --stat            # expect EXACTLY these 4 files
git commit -m "[Agent 0 (Claude), foundation] feat(net): poster scoreboard — arrived/failed/undecodable per host (felt-speed Stage 0)" -- native/net/PosterScoreboard.h native/net/PosterScoreboard.cpp tests/poster_scoreboard_harness.cpp native/CMakeLists.txt
git push
```

---

### Task 2: Wire the scoreboard into the image NAM + boot decoder check + quit dump

**Files:**
- Modify: `native/main.cpp` — four spots: includes; `CachingNam` class (~line 172); `CachingNamFactory` (~line 246); boot wiring at `engine.setNetworkAccessManagerFactory(...)` (~line 535)

- [ ] **Step 1: Add includes**

Near the other includes at the top of `native/main.cpp` add:
```cpp
#include <QImageReader>
#include "net/PosterScoreboard.h"
```

- [ ] **Step 2: Teach CachingNam to watch replies**

In `class CachingNam`, change the constructor signature and member list — the constructor gains a trailing parameter (both existing `useCache=false` call sites are unaffected; they simply never pass a scoreboard):
```cpp
    CachingNam(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost,
               QObject *parent = nullptr, bool useCache = true,
               PosterScoreboard *scoreboard = nullptr)
        : QNetworkAccessManager(parent),
          m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_useCache(useCache),
          m_scoreboard(scoreboard) {
```
Add to the private members (next to `bool m_useCache = true;`):
```cpp
    PosterScoreboard *m_scoreboard = nullptr;

    void watch(const QString &host, QNetworkReply *reply) {
        if (!m_scoreboard)
            return;
        PosterScoreboard *scoreboard = m_scoreboard;
        // No receiver context on purpose: the lambda runs on the reply's own thread and
        // record() is mutex-guarded. `host` is the ORIGINAL hostname — reply->url() may
        // carry the rewritten IPv4 literal for URL-pinned hosts.
        QObject::connect(reply, &QNetworkReply::finished, [scoreboard, host, reply] {
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            scoreboard->record(host, status, ct, reply->bytesAvailable(),
                               reply->error() != QNetworkReply::NoError);
        });
    }
```
In `createRequest`, replace the two return sites at the end:
```cpp
        if (host == QLatin1String("api.jikan.moe")) {
            r.setRawHeader("Accept-Encoding", "gzip");
            QNetworkReply *inner = QNetworkAccessManager::createRequest(op, r, outgoing);
            watch(host, inner);   // watch the INNER reply: GunzipReply doesn't forward attributes
            return new GunzipReply(inner);
        }
        QNetworkReply *reply = QNetworkAccessManager::createRequest(op, r, outgoing);
        watch(host, reply);
        return reply;
```

- [ ] **Step 3: Thread the scoreboard through the factory**

Replace `CachingNamFactory` in full:
```cpp
class CachingNamFactory : public QQmlNetworkAccessManagerFactory {
public:
    CachingNamFactory(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost,
                      PosterScoreboard *scoreboard)
        : m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_scoreboard(scoreboard) {}

    QNetworkAccessManager *create(QObject *parent) override {
        return new CachingNam(m_pinnedHosts, m_ipv4ByHost, parent, /*useCache=*/true, m_scoreboard);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
    PosterScoreboard *m_scoreboard = nullptr;   // owned by the app, outlives every NAM
};
```

- [ ] **Step 4: Boot wiring — decoder check, factory arg, context property, quit dump**

Find `engine.setNetworkAccessManagerFactory(new CachingNamFactory(namPinnedHosts, ipv4ByHost));` and replace with:
```cpp
    // Felt-speed Stage 0: the poster scoreboard. Counts every reply on the QML image
    // NAM; dumped at quit so a real run answers "did the posters actually arrive?"
    // with numbers instead of a shrug. The webp check is the dev-hack scar made loud:
    // the decoder must ship BESIDE the exe (deploy-runtime.bat), not live in the Qt install.
    auto *scoreboard = new PosterScoreboard(&app);
    {
        const bool webpOk =
            QImageReader::supportedImageFormats().contains(QByteArrayLiteral("webp"));
        scoreboard->setWebpDecoderPresent(webpOk);
        if (webpOk)
            qInfo("[img] webp decoder present");
        else
            qWarning("[img] webp decoder MISSING -> every image/webp poster is UNDECODABLE "
                     "(run native/deploy-runtime.bat to bundle qwebp.dll)");
    }
    engine.setNetworkAccessManagerFactory(
        new CachingNamFactory(namPinnedHosts, ipv4ByHost, scoreboard));
    engine.rootContext()->setContextProperty(QStringLiteral("NetScoreboard"), scoreboard);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, scoreboard, [scoreboard] {
        const QString text = scoreboard->summaryText();
        if (!text.isEmpty())
            qInfo("[net] poster scoreboard (arrived/failed/undecodable/bytes by host):\n%s",
                  qUtf8Printable(text));
    });
```

- [ ] **Step 5: Build, then smoke the wiring on a short real run**

Kill any running `colosseum.exe` by PID, then:
```bash
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\build-msvc.bat"
mkdir -p artifacts
QT_FORCE_STDERR_LOGGING=1 ./native/build-msvc/colosseum.exe dev 2> artifacts/stage0-wiring-smoke.log &
sleep 45   # let the home page and a wall load
taskkill //IM colosseum.exe //F
grep -A 12 "poster scoreboard" artifacts/stage0-wiring-smoke.log
grep "webp decoder" artifacts/stage0-wiring-smoke.log
```
Expected: the scoreboard block prints with per-host rows; the webp line prints **MISSING** (the fix is Task 3 — a MISSING here proves the check works on the real binary).

- [ ] **Step 6: Commit (explicit pathspec) and push**

```bash
git add -- native/main.cpp
git diff --cached --stat        # expect EXACTLY native/main.cpp
git commit -m "[Agent 0 (Claude), foundation] feat(net): wire poster scoreboard into the QML image NAM + boot webp check + quit dump" -- native/main.cpp
git push
```

---

### Task 3: Bundle the WebP decoder properly (kill the dev hack)

**Files:**
- Modify: `native/deploy-runtime.bat` (after the `windeployqt` line)
- Modify: `scripts/installer/package_release.sh` (after the `[3/6]` overlay block)

- [ ] **Step 1: Make deploy-runtime.bat bundle qwebp.dll explicitly and loudly**

In `native/deploy-runtime.bat`, directly after the line
`"C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe" --qmldir qml native\build-msvc\colosseum.exe || (echo DEPLOY FAILED & exit /b 1)`
insert:
```bat
REM Felt-speed Stage 0: qwebp.dll never rode windeployqt (the app doesn't link it — it's a
REM runtime-discovered plugin), so WebP covers decoded ONLY on machines where someone
REM hand-dropped the dll into the Qt install. Bundle it explicitly; fail loudly if absent.
copy /Y "C:\Qt\6.11.1\msvc2022_64\plugins\imageformats\qwebp.dll" "native\build-msvc\imageformats\qwebp.dll" || (echo WEBP BUNDLE FAILED & exit /b 1)
echo WEBP_OK
```

- [ ] **Step 2: Guard the installer stage against a webp-less runtime**

In `scripts/installer/package_release.sh`, directly after the `[3/6]` block
(`cp -r "$REPO/native/build-msvc" "$STAGE/native/"`) insert:
```bash
# Felt-speed Stage 0: an installer without the webp decoder ships blank covers. Refuse.
[ -f "$STAGE/native/build-msvc/imageformats/qwebp.dll" ] \
  || { echo "qwebp.dll missing from runtime — run native/deploy-runtime.bat first"; exit 1; }
```

- [ ] **Step 3: Run the deploy and verify the dll landed**

```bash
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\deploy-runtime.bat"
ls native/build-msvc/imageformats/ | grep -i webp
```
Expected: `DEPLOY_OK` preceded by `WEBP_OK`; `qwebp.dll` listed.

- [ ] **Step 4: Prove the boot check flips to PRESENT on the real binary**

Kill any running `colosseum.exe` by PID, then:
```bash
QT_FORCE_STDERR_LOGGING=1 ./native/build-msvc/colosseum.exe dev 2> artifacts/stage0-webp-present.log &
sleep 20
taskkill //IM colosseum.exe //F
grep "webp decoder" artifacts/stage0-webp-present.log
```
Expected: `[img] webp decoder present`

- [ ] **Step 5: Commit (explicit pathspec) and push**

```bash
git add -- native/deploy-runtime.bat scripts/installer/package_release.sh
git diff --cached --stat        # expect EXACTLY these 2 files
git commit -m "[Agent 0 (Claude), foundation] fix(deploy): bundle qwebp.dll beside the exe + installer guard — WebP decode stops being a dev-machine hack" -- native/deploy-runtime.bat scripts/installer/package_release.sh
git push
```

---

### Task 4: Pin the wallpaper host to the fast path

**Files:**
- Modify: `native/main.cpp` — the `pinnedHosts` list (~line 490), anchor: the entry `QStringLiteral("openlibrary.org")`

- [ ] **Step 1: Add wsrv.nl to the pinned-host list**

Change the end of the list from:
```cpp
        QStringLiteral("itunes.apple.com"),
        QStringLiteral("openlibrary.org")
    };
```
to:
```cpp
        QStringLiteral("itunes.apple.com"),
        QStringLiteral("openlibrary.org"),
        // Wallpaper CDN (WallpaperApi.js): unpinned, its requests rode the same dead-AAAA
        // ISP stall as the Jikan scar, so walls silently fell back to the packaged
        // captured-motion asset (humbled-current recap 2026-07-24). Same scar, same fix.
        QStringLiteral("wsrv.nl")
    };
```

- [ ] **Step 2: Build and verify the pin resolves at boot**

Kill any running `colosseum.exe` by PID, then:
```bash
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\build-msvc.bat"
QT_FORCE_STDERR_LOGGING=1 ./native/build-msvc/colosseum.exe dev 2> artifacts/stage0-wsrv-pin.log &
sleep 20
taskkill //IM colosseum.exe //F
grep "IPv4-pinned wsrv.nl" artifacts/stage0-wsrv-pin.log
```
Expected: `[net] IPv4-pinned wsrv.nl -> <some IPv4>` (if instead the "NO IPv4" warning prints, the pin still fails fast rather than stalling — report it, don't hide it).

- [ ] **Step 3: Commit (explicit pathspec) and push**

```bash
git add -- native/main.cpp
git diff --cached --stat        # expect EXACTLY native/main.cpp
git commit -m "[Agent 0 (Claude), foundation] fix(net): pin wsrv.nl (wallpaper CDN) — same dead-AAAA scar, same IPv4 fix" -- native/main.cpp
git push
```

---

### Task 5: The measured run + Hemanth's eyes (the Stage 0 gate)

**Files:** none created except the evidence log under `artifacts/` (untracked).

- [ ] **Step 1: Rebuild from the COMMITTED tree and redeploy (verify-the-committed-artifact rule)**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum"
git status --short | grep -v "^??"     # expect ONLY ' M wallpapers.ini' (runtime noise)
```
Kill any running `colosseum.exe` by PID, then:
```bash
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\build-msvc.bat"
cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\deploy-runtime.bat"
```
Expected: build OK, `WEBP_OK`, `DEPLOY_OK`.

- [ ] **Step 2: The measured run**

Launch the real binary with logging:
```bash
QT_FORCE_STDERR_LOGGING=1 ./native/build-msvc/colosseum.exe dev 2> artifacts/stage0-measured-run.log &
```
Browse (agent may drive if unattended, but prefer Hemanth at the wheel): Theatre home wall + one series page, Tankoban covers, Biblio shelf, the wallpaper surface. Then quit the app normally (the quit dump needs a clean exit).

- [ ] **Step 3: Read the verdict**

```bash
grep "webp decoder" artifacts/stage0-measured-run.log
grep -A 15 "poster scoreboard" artifacts/stage0-measured-run.log
```
Acceptance (from the spec §8):
- `webp decoder present`
- `undecodable == 0` on every host
- `failed` ≈ 0 on the art hosts (`images.metahub.space`, `live.metahub.space`, `wsrv.nl`, `uploads.mangadex.org`) — occasional stragglers are fine; a systematically failing host is a finding to report with its row, not to explain away.

If a host still fails systematically: **STOP. Do not improvise fixes.** The scoreboard row IS the deliverable — report it to Hemanth; the convicted culprit gets its own scoped fix.

- [ ] **Step 4: Hemanth's eyes — the gate itself**

Hemanth opens the app and looks at the shelves. **Full posters = Stage 0 passes.** Only his eyes close this stage (report metrics ≠ eyes; the scoreboard is evidence, not the verdict).

- [ ] **Step 5: Bank the evidence**

Post the scoreboard block + verdict to `agents/chat.md` as an RTC line, and note gate-passed in the session recap. Stages 1–3 planning unlocks only now.

---

## Self-review (run against the spec)

- **Spec coverage (Stage 0):** scoreboard counts with reasons per host → Tasks 1–2. WebP bundled beside the exe + boot verification → Tasks 2–3. Host pinning → Task 4. Real-binary measured run + eyes-on gate → Task 5. Boot decoder inventory surfaced in scoreboard → Task 2 Step 4 (`setWebpDecoderPresent`).
- **Out of scope honored:** no Stage 1–3 work, no snapshot store, no warming, no budgets — those plans come after the gate. `NetScoreboard` context property is the single forward hook Stage 3's readout will consume (spec §7 requires the scoreboard feed it).
- **Placeholder scan:** every code step carries complete code; every run step carries the exact command and expected output. No TBDs.
- **Type consistency:** `PosterScoreboard::Bucket`, `classify(int, QString, bool, bool)`, `record(host, status, ct, bytes, err)`, `summary()`, `summaryText()`, `setWebpDecoderPresent(bool)` are identical across Tasks 1, 2, and the harness.
- **House rules encoded:** explicit-pathspec commits with `--cached` verification (every commit step), commit+push together, absolute-path bat invocation, kill-by-PID before builds, nested-repo `cd`, stash untouched, A2's revert landed first and attributed.
