#include "update/UpdateService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <cmath>
#include <iostream>

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

    std::cout << "UPDATE_SERVICE_OK\n";
}
