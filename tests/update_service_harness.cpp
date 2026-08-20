#include "update/UpdateService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>

using namespace Colosseum::Update;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

Version version(const char* text)
{
    const auto parsed = Version::parseCanonical(QString::fromLatin1(text));
    require(parsed.has_value(), "fixture version parses");
    return *parsed;
}

Manifest manifestFor(const char* versionText, const char* minimumText = "1.1.0")
{
    const Version target = version(versionText);
    Manifest manifest;
    manifest.schemaVersion = 1;
    manifest.version = target;
    manifest.tag = QStringLiteral("v") + target.canonical();
    manifest.eyebrow = QStringLiteral("A NEW CHAPTER IS READY");
    manifest.title = QStringLiteral("Colosseum ") + target.canonical();
    manifest.summary = QStringLiteral("The house keeps itself current.");
    manifest.installerAsset = QStringLiteral("Colosseum-setup.exe");
    manifest.installerSize = 17;
    manifest.installerSha256 = QByteArray(32, '\x11');
    manifest.minimumUpdaterVersion = version(minimumText);
    manifest.notesUrl = QStringLiteral("https://github.com/kingoftheseas56/Colosseum/releases/tag/")
        + manifest.tag;
    return manifest;
}

ReleaseCheckResult validResult(const Manifest& manifest)
{
    ReleaseCheckResult result;
    result.status = ReleaseCheckResult::Status::Valid;
    result.manifest = manifest;
    result.etag = QStringLiteral("\"fixture-etag\"");
    result.assetUrls.insert(manifest.installerAsset,
                            QUrl(QStringLiteral("http://127.0.0.1/Colosseum-setup.exe")));
    for (const Artwork& artwork : manifest.artwork)
        result.assetUrls.insert(artwork.assetName,
                                QUrl(QStringLiteral("http://127.0.0.1/") + artwork.assetName));
    return result;
}

bool copyTree(const QString& source, const QString& destination)
{
    QDir sourceDir(source);
    if (!sourceDir.exists() || !QDir().mkpath(destination))
        return false;
    for (const QFileInfo& info : sourceDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                         QDir::Name)) {
        const QString target = QDir(destination).filePath(info.fileName());
        if (info.isDir()) {
            if (!copyTree(info.absoluteFilePath(), target))
                return false;
        } else if (!QFile::copy(info.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}

ReleaseCheckResult signedLanistaResult()
{
    const QString root = QStringLiteral(COLOSSEUM_UPDATE_FIXTURES_DIR)
        + QStringLiteral("/update-available/updates/1.1.1/");
    QFile manifestFile(root + QStringLiteral("manifest.json"));
    QFile signatureFile(root + QStringLiteral("manifest.sig"));
    require(manifestFile.open(QIODevice::ReadOnly), "signed fixture manifest opens");
    require(signatureFile.open(QIODevice::ReadOnly), "signed fixture signature opens");
    const QByteArray manifestBytes = manifestFile.readAll();
    const QByteArray signatureBytes = QByteArray::fromHex(signatureFile.readAll().trimmed());
    QString error;
    const auto parsed = parseManifest(manifestBytes, &error);
    require(parsed.has_value(), "signed fixture manifest parses");
    ReleaseCheckResult result = validResult(*parsed);
    result.verifiedManifestBytes = manifestBytes;
    result.verifiedSignatureBytes = signatureBytes;
    return result;
}

// Drives a fresh UpdateService through checkNow -> download -> progress ->
// onCompleted so it lands on Ready, matching the recipe the top-of-file
// lifecycle test uses (see the `service.restartAndUpdate()` block above).
// Callers supply the releaseResult so signed fixtures (which populate
// verifiedManifestBytes/Signature and make persist() durable) and unsigned
// fixtures (persist() no-ops) both work through the same path.
std::unique_ptr<UpdateService> readyService(UpdateServiceHooks hooks, const QString& cacheRoot,
                                            const QString& installerPath,
                                            const ReleaseCheckResult& releaseResult)
{
    hooks.checkLatest = [releaseResult](const QString&, UpdateReleaseClient::Callback done) {
        done(releaseResult);
    };
    auto progress = std::make_shared<UpdateServiceHooks::DownloadProgress>();
    auto completed = std::make_shared<UpdateServiceHooks::DownloadCompleted>();
    auto failed = std::make_shared<UpdateServiceHooks::DownloadFailed>();
    hooks.startDownload = [progress, completed, failed](
        const DownloadRequest&, UpdateServiceHooks::DownloadProgress p,
        UpdateServiceHooks::DownloadCompleted c, UpdateServiceHooks::DownloadFailed f) {
        *progress = std::move(p);
        *completed = std::move(c);
        *failed = std::move(f);
    };
    auto service = std::make_unique<UpdateService>(version("1.1.0"), cacheRoot, hooks);
    service->checkNow();
    service->download();
    require(bool(*progress) && bool(*completed), "readyService fixture wires download callbacks");
    (*progress)(17, 17, 100);
    (*completed)(installerPath);
    return service;
}

} // namespace

int main()
{
    qint64 clock = 1000;
    ReleaseCheckResult pending = validResult(manifestFor("1.2.0"));
    int checkCalls = 0;
    int downloadCalls = 0;
    int cancelCalls = 0;
    UpdateServiceHooks::DownloadProgress onProgress;
    UpdateServiceHooks::DownloadCompleted onCompleted;
    UpdateServiceHooks::DownloadFailed onFailed;
    bool launcherCalled = false;

    UpdateServiceHooks hooks;
    hooks.nowMs = [&clock] { return clock; };
    hooks.checkLatest = [&checkCalls, &pending](const QString&, UpdateReleaseClient::Callback done) {
        ++checkCalls;
        done(pending);
    };
    hooks.startDownload = [&downloadCalls, &onProgress, &onCompleted, &onFailed](
                              const DownloadRequest&, UpdateServiceHooks::DownloadProgress progress,
                              UpdateServiceHooks::DownloadCompleted completed,
                              UpdateServiceHooks::DownloadFailed failed) {
        ++downloadCalls;
        onProgress = std::move(progress);
        onCompleted = std::move(completed);
        onFailed = std::move(failed);
    };
    hooks.cancelDownload = [&cancelCalls] { ++cancelCalls; };
    hooks.installLauncher = [&launcherCalled](const QString& path, const Version&, QString*) {
        launcherCalled = !path.isEmpty();
        return launcherCalled;
    };

    QTemporaryDir lifecycleRoot;
    require(lifecycleRoot.isValid(), "lifecycle temp root");
    UpdateService service(version("1.1.0"), lifecycleRoot.path(), hooks);
    require(service.state() == UpdateService::Idle, "first service starts idle");
    service.checkNow();
    require(service.state() == UpdateService::Available, "newer release becomes available");
    require(service.updateAvailable() && service.unseenUpdate(), "available release is unseen");

    pending = validResult(manifestFor("1.0.9"));
    service.checkNow();
    require(service.state() == UpdateService::UpToDate && !service.updateAvailable(),
            "older release is treated as up to date");
    pending = validResult(manifestFor("1.1.0"));
    service.checkNow();
    require(service.state() == UpdateService::UpToDate, "equal release is up to date");
    pending = validResult(manifestFor("1.2.0"));
    service.checkNow();
    require(service.state() == UpdateService::Available, "newer release is available again");

    const QString chronicleVersion = service.latestVersion();
    pending.status = ReleaseCheckResult::Status::Rejected;
    pending.errorCode = QStringLiteral("invalid_manifest");
    service.checkNow();
    require(service.state() == UpdateService::Available
                && service.latestVersion() == chronicleVersion,
            "rejected release preserves the chronicle");
    pending.status = ReleaseCheckResult::Status::NetworkError;
    service.checkNow();
    require(service.state() == UpdateService::Available, "network failure is quiet");

    service.markSeen();
    require(!service.unseenUpdate() && service.updateAvailable(),
            "markSeen clears only the unseen badge");
    const int beforeThrottle = checkCalls;
    service.startAutomaticChecks();
    require(checkCalls == beforeThrottle, "automatic checks are throttled");
    clock += 6LL * 60LL * 60LL * 1000LL;
    pending = validResult(manifestFor("1.2.0"));
    service.startAutomaticChecks();
    require(checkCalls == beforeThrottle + 1, "automatic check runs after six hours");
    service.checkNow();
    require(checkCalls == beforeThrottle + 2, "manual check bypasses throttle");

    service.download();
    require(service.state() == UpdateService::Downloading && downloadCalls == 1,
            "download enters streaming state");
    onProgress(4, 17, 100);
    require(service.receivedBytes() == 4 && service.state() == UpdateService::Downloading,
            "download progress is exposed");
    service.cancelDownload();
    require(service.state() == UpdateService::Paused && cancelCalls == 1,
            "cancel pauses a resumable download");
    service.download();
    require(downloadCalls == 2, "paused download resumes");
    onFailed(QStringLiteral("network_error"), true);
    require(service.state() == UpdateService::Paused, "resumable failure stays paused");
    service.download();
    onProgress(17, 17, 100);
    require(service.state() == UpdateService::Verifying, "complete bytes enter verification");
    QTemporaryDir installerRoot;
    const QString installer = QDir(installerRoot.path()).filePath(QStringLiteral("setup.exe"));
    QFile installerFile(installer);
    require(installerFile.open(QIODevice::WriteOnly), "fake installer opens");
    installerFile.write("verified");
    installerFile.close();
    onCompleted(installer);
    require(service.state() == UpdateService::Ready, "verified download is ready");
    service.restartAndUpdate();
    require(launcherCalled && service.state() == UpdateService::Installing,
            "ready state calls the injected launcher");

    pending = validResult(manifestFor("1.3.0"));
    service.checkNow();
    service.download();
    onFailed(QStringLiteral("sha256_mismatch"), false);
    require(service.state() == UpdateService::VerificationFailure
                && !service.updateAvailable(),
            "failed target is suppressed after verification failure");
    pending = validResult(manifestFor("1.4.0"));
    service.checkNow();
    require(service.state() == UpdateService::Available && service.updateAvailable(),
            "superseding release clears failed-target suppression");
    pending = validResult(manifestFor("2.0.0", "9.0.0"));
    service.checkNow();
    require(service.state() == UpdateService::ManualUpdateRequired,
            "minimum updater requirement selects manual path");

    QTemporaryDir persistedRoot;
    const ReleaseCheckResult signedResult = signedLanistaResult();
    UpdateServiceHooks signedHooks;
    signedHooks.nowMs = [&clock] { return clock; };
    signedHooks.checkLatest = [signedResult](const QString&, UpdateReleaseClient::Callback done) {
        done(signedResult);
    };
    UpdateService persisted(version("1.1.0"), persistedRoot.path(), signedHooks);
    persisted.checkNow();
    require(persisted.state() == UpdateService::Available, "signed chronicle becomes available");
    UpdateService offline(version("1.1.0"), persistedRoot.path());
    require(offline.state() == UpdateService::Available
                && offline.latestVersion() == QStringLiteral("1.1.1"),
            "offline restart reconstructs the signed chronicle");

    QTemporaryDir noChronicleRoot;
    UpdateService noChronicle(version("1.1.0"), noChronicleRoot.path());
    noChronicle.setTestingPresentationState(UpdateService::Downloading,
                                            224395264, 330301440);
    require(noChronicle.state() == UpdateService::Idle
                && noChronicle.receivedBytes() == 0
                && noChronicle.totalBytes() == 0,
            "presentation override cannot run without an authenticated chronicle");

    persisted.setTestingPresentationState(UpdateService::Downloading,
                                          224395264, 330301440);
    require(persisted.state() == UpdateService::Downloading
                && persisted.receivedBytes() == 224395264
                && persisted.totalBytes() == 330301440
                && std::abs(persisted.progress() - (224395264.0 / 330301440.0)) < 0.000001,
            "downloading presentation exposes the exact fixture progress");
    persisted.setTestingPresentationState(UpdateService::Paused, 224395264, 330301440);
    require(persisted.state() == UpdateService::Paused,
            "paused presentation state is accepted");
    persisted.setTestingPresentationState(UpdateService::Verifying, 330301440, 330301440);
    require(persisted.state() == UpdateService::Verifying,
            "verifying presentation state is accepted");
    persisted.setTestingPresentationState(UpdateService::Ready, 330301440, 330301440);
    require(persisted.state() == UpdateService::Ready,
            "ready presentation state is accepted");
    persisted.setTestingPresentationState(UpdateService::Idle, 0, 0);
    require(persisted.state() == UpdateService::Ready,
            "unsupported presentation state is ignored");
    UpdateService afterPresentation(version("1.1.0"), persistedRoot.path());
    require(afterPresentation.state() == UpdateService::Available
                && afterPresentation.receivedBytes() == 0
                && afterPresentation.progress() == 0.0
                && afterPresentation.totalBytes() != 330301440,
            "presentation override is not persisted across restart");

    QTemporaryDir artworkRoot;
    QDir().mkpath(QDir(artworkRoot.path()).filePath(QStringLiteral("artwork")));
    const QByteArray artworkBytes("artwork-good");
    QFile artworkFile(QDir(artworkRoot.path()).filePath(QStringLiteral("artwork/hero.png")));
    require(artworkFile.open(QIODevice::WriteOnly), "artwork fixture opens");
    artworkFile.write(artworkBytes);
    artworkFile.close();
    Manifest illustrated = manifestFor("1.5.0");
    illustrated.highlights.append(Highlight{HighlightKind::Feature, QStringLiteral("NOW"),
                                            QStringLiteral("A feature"), QStringLiteral("Details"),
                                            {}, {}, {}, {}, {QStringLiteral("hero.png")} });
    Artwork hero;
    hero.assetName = QStringLiteral("hero.png");
    hero.sha256 = QCryptographicHash::hash(artworkBytes, QCryptographicHash::Sha256);
    illustrated.artwork.append(hero);
    QByteArray artworkResponse = artworkBytes;
    UpdateServiceHooks illustratedHooks;
    illustratedHooks.checkLatest = [illustrated](const QString&, UpdateReleaseClient::Callback done) {
        done(validResult(illustrated));
    };
    illustratedHooks.fetchArtwork = [&artworkResponse](const QString&, const QUrl&, qint64,
                                                        UpdateServiceHooks::ArtworkCompleted done,
                                                        UpdateServiceHooks::ArtworkFailed) {
        done(artworkResponse);
    };
    UpdateService illustratedService(version("1.1.0"), artworkRoot.path(), illustratedHooks);
    illustratedService.checkNow();
    require(!illustratedService.highlights().isEmpty()
                && !illustratedService.highlights().at(0).toMap().value(QStringLiteral("artwork"))
                       .toList().isEmpty(),
            "verified artwork is exposed as a local file URL");
    require(QFile::remove(artworkFile.fileName()), "artwork removed for fetch test");
    illustratedService.checkNow();
    require(QFile::exists(artworkFile.fileName()), "missing artwork fetched into cache atomically");
    artworkResponse = QByteArray("corrupt");
    artworkFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    artworkFile.write("corrupt");
    artworkFile.close();
    illustratedService.checkNow();
    require(illustratedService.latestVersion() == QStringLiteral("1.5.0")
                && illustratedService.highlights().at(0).toMap()
                       .value(QStringLiteral("artwork")).toList().isEmpty(),
            "corrupt artwork falls back without losing release copy");

    QTemporaryDir seedAvailable;
    QTemporaryDir seedUpToDate;
    require(copyTree(QStringLiteral(COLOSSEUM_UPDATE_FIXTURES_DIR)
                         + QStringLiteral("/update-available/updates"), seedAvailable.path()),
            "available Lanista seed copied");
    require(copyTree(QStringLiteral(COLOSSEUM_UPDATE_FIXTURES_DIR)
                         + QStringLiteral("/update-up-to-date/updates"), seedUpToDate.path()),
            "up-to-date Lanista seed copied");
    UpdateService seededAvailable(version("1.1.0"), seedAvailable.path());
    require(seededAvailable.state() == UpdateService::Available,
            "available Lanista seed loads without network");
    UpdateService seededUpToDate(version("1.1.1"), seedUpToDate.path());
    require(seededUpToDate.state() == UpdateService::UpToDate,
            "up-to-date Lanista seed loads without network");
    const QString badSignature = QDir(seedAvailable.path()).filePath(
        QStringLiteral("1.1.1/manifest.sig"));
    QFile badFile(badSignature);
    require(badFile.open(QIODevice::ReadOnly), "seed signature opens for mutation");
    QByteArray mutated = badFile.readAll();
    badFile.close();
    mutated[0] = mutated[0] == '0' ? '1' : '0';
    require(badFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "mutated seed signature opens");
    badFile.write(mutated);
    badFile.close();
    UpdateService rejectedSeed(version("1.1.0"), seedAvailable.path());
    require(rejectedSeed.state() == UpdateService::Idle
                && rejectedSeed.latestVersion().isEmpty(),
            "mutated seed signature never reaches visible state");

    // ── Installed chronicle (Slice 3): at rest, the gallery renders the
    // installed release's chapters from the bundled signed seed. The offered-
    // release selector flips to a newer release on Available and back on
    // withdrawal. offeredReleaseChanged is discrete: fires only on the
    // installed↔newer flip, never on state-only transitions. ───────────────
    {
        const QString chronicleDir = QStringLiteral(COLOSSEUM_UPDATE_FIXTURES_DIR)
            + QStringLiteral("/installed-chronicle");
        const QString manifestPath = QDir(chronicleDir).filePath(QStringLiteral("manifest.json"));
        const QString signaturePath = QDir(chronicleDir).filePath(QStringLiteral("manifest.sig"));
        const QString artworkRoot = QDir(chronicleDir).filePath(QStringLiteral("artwork"));

        // (g) Seed-absence negative control: no chronicle hooks → empty at rest.
        {
            QTemporaryDir noChronicleRoot;
            require(noChronicleRoot.isValid(), "no-chronicle temp root");
            UpdateServiceHooks emptyHooks;
            emptyHooks.nowMs = [&clock] { return clock; };
            UpdateService noChronicle(version("1.1.0"), noChronicleRoot.path(), emptyHooks);
            require(noChronicle.state() == UpdateService::Idle, "no-chronicle starts idle");
            require(noChronicle.highlights().isEmpty(),
                    "no installed chronicle seed -> empty highlights at rest");
            require(noChronicle.release().isEmpty(),
                    "no installed chronicle seed -> empty release at rest");
        }

        // (a)(b) At rest, the installed chronicle's chapters render.
        // (c)(d)(f) Flip on Available, return on withdrawal, discrete signal.
        {
            QTemporaryDir flipRoot;
            require(flipRoot.isValid(), "flip-test temp root");
            UpdateServiceHooks chronicleHooks;
            chronicleHooks.nowMs = [&clock] { return clock; };
            chronicleHooks.installedChronicleManifestPath = manifestPath;
            chronicleHooks.installedChronicleSignaturePath = signaturePath;
            chronicleHooks.installedChronicleArtworkRoot = artworkRoot;
            chronicleHooks.checkLatest =
                [&pending](const QString&, UpdateReleaseClient::Callback done) { done(pending); };
            int offeredChanges = 0;
            UpdateService service(version("1.1.0"), flipRoot.path(), chronicleHooks);
            QObject::connect(&service, &UpdateService::offeredReleaseChanged,
                             [&offeredChanges] { ++offeredChanges; });

            // (a)(b) At rest: five chapters, version 1.1.0, first chapter Reader.
            require(service.state() == UpdateService::Idle, "at-rest starts idle");
            require(service.highlights().size() == 5,
                    "installed chronicle exposes five chapters at rest");
            require(service.release().value(QStringLiteral("version")).toString()
                        == QStringLiteral("1.1.0"),
                    "installed chronicle version matches installed release");
            require(service.highlights().at(0).toMap().value(QStringLiteral("title")).toString()
                        == QStringLiteral("Reader"),
                    "installed chronicle first chapter is Reader");
            require(offeredChanges == 0,
                    "offeredReleaseChanged does not fire at construction (no flip)");

            // (c) On Available, the offered release flips to the newer one.
            // manifestFor produces a minimal manifest (zero highlights); the
            // contract is the selector flip, proven by version + the discrete
            // signal, not by highlight count.
            pending = validResult(manifestFor("1.2.0"));
            service.checkNow();
            require(service.state() == UpdateService::Available,
                    "newer release becomes available");
            require(service.release().value(QStringLiteral("version")).toString()
                        == QStringLiteral("1.2.0"),
                    "on Available, release version is the newer release");
            require(service.highlights().size() != 5,
                    "on Available, highlights no longer reflect the installed chronicle");
            require(offeredChanges == 1,
                    "offeredReleaseChanged fires exactly once on installed->newer flip");

            // (d) On withdrawal (older release -> UpToDate), the offered release
            // returns to the installed chronicle.
            pending = validResult(manifestFor("1.0.9"));
            service.checkNow();
            require(service.state() == UpdateService::UpToDate,
                    "older release returns to up to date");
            require(service.highlights().size() == 5,
                    "on withdrawal, highlights returns to installed chronicle");
            require(service.release().value(QStringLiteral("version")).toString()
                        == QStringLiteral("1.1.0"),
                    "on withdrawal, release version returns to installed");
            require(offeredChanges == 2,
                    "offeredReleaseChanged fires exactly once on newer->installed flip");

            // (f) Negative: a state-only transition (another older release,
            // stays UpToDate) must NOT fire offeredReleaseChanged.
            const int beforeStateOnly = offeredChanges;
            pending = validResult(manifestFor("1.0.8"));
            service.checkNow();
            require(service.state() == UpdateService::UpToDate,
                    "another older release stays up to date");
            require(offeredChanges == beforeStateOnly,
                    "offeredReleaseChanged does NOT fire on a state-only transition");
        }

        // (e) Corrupted bundle -> empty fallback (honest degradation).
        {
            QTemporaryDir corruptRoot;
            require(corruptRoot.isValid(), "corrupt-bundle temp root");
            const QString corruptDir = QDir(corruptRoot.path()).filePath(QStringLiteral("ic"));
            QDir().mkpath(corruptDir);
            QFile::copy(manifestPath, QDir(corruptDir).filePath(QStringLiteral("manifest.json")));
            QFile sigOriginal(signaturePath);
            require(sigOriginal.open(QIODevice::ReadOnly), "fixture sig opens for corrupt copy");
            QByteArray sigBytes = sigOriginal.readAll();
            sigOriginal.close();
            sigBytes[0] = sigBytes.at(0) == '0' ? '1' : '0';
            QFile sigOut(QDir(corruptDir).filePath(QStringLiteral("manifest.sig")));
            require(sigOut.open(QIODevice::WriteOnly | QIODevice::Truncate), "corrupt sig opens");
            sigOut.write(sigBytes);
            sigOut.close();
            UpdateServiceHooks corruptHooks;
            corruptHooks.nowMs = [&clock] { return clock; };
            corruptHooks.installedChronicleManifestPath =
                QDir(corruptDir).filePath(QStringLiteral("manifest.json"));
            corruptHooks.installedChronicleSignaturePath =
                QDir(corruptDir).filePath(QStringLiteral("manifest.sig"));
            corruptHooks.installedChronicleArtworkRoot = artworkRoot;
            UpdateService corrupt(version("1.1.0"), corruptRoot.path(), corruptHooks);
            require(corrupt.state() == UpdateService::Idle, "corrupt bundle starts idle");
            require(corrupt.highlights().isEmpty(),
                    "corrupted installed chronicle -> empty highlights (honest degradation)");
            require(corrupt.release().isEmpty(),
                    "corrupted installed chronicle -> empty release (no unverified artwork)");
        }
    }

    // ── Shutdown lifecycle (Arc 11 slice 1): restartAndUpdate() requests an
    // explicit process shutdown after a successful launch + persist(), so the
    // installer's /WAITPID=<our PID> wait resolves instead of idling out its
    // 120s timeout. requestShutdown is null-checked; unset must never crash. ──
    {
        // (1)(2) Success path: shutdown requested exactly once, and by the time
        // the callback fires Installing is already durably persisted — proven
        // by constructing a second UpdateService on the same cacheRoot from
        // INSIDE the callback and requiring it restores to Installing. Uses the
        // signed fixture (not manifestFor/validResult) because persist() only
        // writes once verifiedManifestBytes/Signature are populated.
        QTemporaryDir shutdownRoot;
        require(shutdownRoot.isValid(), "shutdown-success temp root");
        QTemporaryDir shutdownInstallerDir;
        const QString shutdownInstaller =
            QDir(shutdownInstallerDir.path()).filePath(QStringLiteral("setup.exe"));
        QFile shutdownInstallerFile(shutdownInstaller);
        require(shutdownInstallerFile.open(QIODevice::WriteOnly), "shutdown fixture installer opens");
        shutdownInstallerFile.write("verified");
        shutdownInstallerFile.close();

        UpdateServiceHooks shutdownHooks;
        shutdownHooks.installLauncher = [](const QString& path, const Version&, QString*) {
            return !path.isEmpty();
        };
        int shutdownCalls = 0;
        bool observedPersistedStateAtShutdown = false;
        UpdateService::State persistedStateAtShutdown = UpdateService::Idle;
        const QString shutdownCacheRoot = shutdownRoot.path();
        shutdownHooks.requestShutdown = [&shutdownCalls, &persistedStateAtShutdown,
                                          &observedPersistedStateAtShutdown, shutdownCacheRoot] {
            ++shutdownCalls;
            UpdateService reloaded(version("1.1.0"), shutdownCacheRoot);
            persistedStateAtShutdown = reloaded.state();
            observedPersistedStateAtShutdown = true;
        };
        auto readySvc = readyService(shutdownHooks, shutdownCacheRoot, shutdownInstaller,
                                     signedLanistaResult());
        require(readySvc->state() == UpdateService::Ready, "shutdown-success fixture reaches Ready");
        readySvc->restartAndUpdate();
        require(shutdownCalls == 1, "successful launch requests shutdown exactly once");
        require(readySvc->state() == UpdateService::Installing,
                "successful launch leaves state Installing");
        require(observedPersistedStateAtShutdown
                    && persistedStateAtShutdown == UpdateService::Installing,
                "Installing is persisted before requestShutdown fires");

        // (3) Launch failure: no shutdown, state becomes RecoverableError.
        QTemporaryDir failRoot;
        require(failRoot.isValid(), "shutdown-failure temp root");
        QTemporaryDir failInstallerDir;
        const QString failInstaller =
            QDir(failInstallerDir.path()).filePath(QStringLiteral("setup.exe"));
        QFile failInstallerFile(failInstaller);
        require(failInstallerFile.open(QIODevice::WriteOnly), "failure fixture installer opens");
        failInstallerFile.write("verified");
        failInstallerFile.close();

        UpdateServiceHooks failHooks;
        failHooks.installLauncher = [](const QString&, const Version&, QString* error) {
            if (error)
                *error = QStringLiteral("launch_failed");
            return false;
        };
        int failShutdownCalls = 0;
        failHooks.requestShutdown = [&failShutdownCalls] { ++failShutdownCalls; };
        auto failSvc = readyService(failHooks, failRoot.path(), failInstaller,
                                    validResult(manifestFor("1.2.0")));
        require(failSvc->state() == UpdateService::Ready, "shutdown-failure fixture reaches Ready");
        failSvc->restartAndUpdate();
        require(failShutdownCalls == 0, "failed launch never requests shutdown");
        require(failSvc->state() == UpdateService::RecoverableError,
                "failed launch leaves state RecoverableError");

        // (4) Non-Ready call: restartAndUpdate() from Idle is a no-op, no shutdown.
        QTemporaryDir idleRoot;
        require(idleRoot.isValid(), "shutdown-idle temp root");
        UpdateServiceHooks idleHooks;
        int idleShutdownCalls = 0;
        idleHooks.requestShutdown = [&idleShutdownCalls] { ++idleShutdownCalls; };
        idleHooks.installLauncher = [](const QString&, const Version&, QString*) { return true; };
        UpdateService idleSvc(version("1.1.0"), idleRoot.path(), idleHooks);
        require(idleSvc.state() == UpdateService::Idle, "idle fixture starts idle");
        idleSvc.restartAndUpdate();
        require(idleShutdownCalls == 0,
                "restartAndUpdate from a non-Ready state never requests shutdown");
        require(idleSvc.state() == UpdateService::Idle,
                "restartAndUpdate from a non-Ready state leaves state untouched");

        // (5) Hook unset: success path must not crash without a requestShutdown hook.
        QTemporaryDir unsetRoot;
        require(unsetRoot.isValid(), "shutdown-unset temp root");
        QTemporaryDir unsetInstallerDir;
        const QString unsetInstaller =
            QDir(unsetInstallerDir.path()).filePath(QStringLiteral("setup.exe"));
        QFile unsetInstallerFile(unsetInstaller);
        require(unsetInstallerFile.open(QIODevice::WriteOnly), "unset-hook fixture installer opens");
        unsetInstallerFile.write("verified");
        unsetInstallerFile.close();

        UpdateServiceHooks unsetHooks;
        unsetHooks.installLauncher = [](const QString& path, const Version&, QString*) {
            return !path.isEmpty();
        };
        // unsetHooks.requestShutdown intentionally left default-constructed (unset).
        auto unsetSvc = readyService(unsetHooks, unsetRoot.path(), unsetInstaller,
                                     validResult(manifestFor("1.2.0")));
        require(unsetSvc->state() == UpdateService::Ready, "unset-hook fixture reaches Ready");
        unsetSvc->restartAndUpdate();
        require(unsetSvc->state() == UpdateService::Installing,
                "unset requestShutdown hook does not crash the success path");
    }

    std::cout << "UPDATE_SERVICE_OK\n";
}
