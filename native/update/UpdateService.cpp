#include "update/UpdateService.h"

#include "update/UpdateTrust.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QDateTime>

namespace Colosseum::Update {
namespace {

constexpr qint64 kAutomaticCheckIntervalMs = 6LL * 60LL * 60LL * 1000LL;
constexpr qint64 kArtworkCapBytes = 8LL * 1024LL * 1024LL;
constexpr int kServiceSchemaVersion = 1;

QString normalizedRoot(QString root)
{
    return QDir::cleanPath(root.isEmpty() ? UpdateCache::productionRoot() : std::move(root));
}

void fail(QString* error, const QString& message)
{
    if (error) *error = message;
}

QString highlightKindName(HighlightKind kind)
{
    switch (kind) {
    case HighlightKind::Feature: return QStringLiteral("feature");
    case HighlightKind::Statistic: return QStringLiteral("statistic");
    case HighlightKind::BeforeAfter: return QStringLiteral("beforeAfter");
    case HighlightKind::Milestone: return QStringLiteral("milestone");
    }
    return {};
}

bool safeRelativePath(const QString& root, const QString& relative, QString* result)
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative))
        return false;
    const QString cleanRoot = QDir::cleanPath(root);
    const QString clean = QDir::cleanPath(QDir(cleanRoot).filePath(relative));
    if (clean != cleanRoot && !clean.startsWith(cleanRoot + QLatin1Char('/')))
        return false;
    if (result) *result = clean;
    return true;
}

QByteArray readVerifiedSignature(QByteArray bytes)
{
    const QByteArray text = bytes.trimmed();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{128}$"));
    if (hex.match(QString::fromLatin1(text)).hasMatch())
        return QByteArray::fromHex(text);
    return bytes;
}

QVariantMap highlightMap(const Highlight& highlight, const QString& artworkRoot,
                         const QHash<QString, QByteArray>& artworkDigests)
{
    QVariantMap map;
    map.insert(QStringLiteral("kind"), highlightKindName(highlight.kind));
    map.insert(QStringLiteral("section"), highlight.section);
    map.insert(QStringLiteral("title"), highlight.title);
    map.insert(QStringLiteral("body"), highlight.body);
    map.insert(QStringLiteral("value"), highlight.value);
    map.insert(QStringLiteral("context"), highlight.context);
    map.insert(QStringLiteral("beforeCaption"), highlight.beforeCaption);
    map.insert(QStringLiteral("afterCaption"), highlight.afterCaption);

    QVariantList artwork;
    for (const QString& asset : highlight.artworkAssets) {
        const QString path = QDir(artworkRoot).filePath(asset);
        QFileInfo info(path);
        if (!info.isFile() || info.size() < 0 || info.size() > kArtworkCapBytes)
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file))
            continue;
        const auto expected = artworkDigests.constFind(asset);
        if (expected == artworkDigests.constEnd() || hash.result() != expected.value())
            continue;
        artwork.append(QUrl::fromLocalFile(info.absoluteFilePath()).toString());
    }
    map.insert(QStringLiteral("artwork"), artwork);
    return map;
}

} // namespace

UpdateService::UpdateService(Version installedVersion, QString cacheRoot,
                             UpdateServiceHooks hooks, QObject* parent)
    : QObject(parent),
      m_installedVersion(installedVersion),
      m_cacheRoot(normalizedRoot(std::move(cacheRoot))),
      m_cache(m_cacheRoot),
      m_hooks(std::move(hooks))
{
    loadPersisted();
}

double UpdateService::progress() const
{
    if (m_totalBytes <= 0)
        return 0.0;
    return qBound(0.0, static_cast<double>(m_receivedBytes)
                      / static_cast<double>(m_totalBytes), 1.0);
}

qint64 UpdateService::nowMs() const
{
    return m_hooks.nowMs ? m_hooks.nowMs() : QDateTime::currentMSecsSinceEpoch();
}

QString UpdateService::stateName(State state)
{
    switch (state) {
    case Idle: return QStringLiteral("Idle");
    case Checking: return QStringLiteral("Checking");
    case UpToDate: return QStringLiteral("UpToDate");
    case Available: return QStringLiteral("Available");
    case Downloading: return QStringLiteral("Downloading");
    case Paused: return QStringLiteral("Paused");
    case Verifying: return QStringLiteral("Verifying");
    case Ready: return QStringLiteral("Ready");
    case Installing: return QStringLiteral("Installing");
    case RecoverableError: return QStringLiteral("RecoverableError");
    case VerificationFailure: return QStringLiteral("VerificationFailure");
    case ManualUpdateRequired: return QStringLiteral("ManualUpdateRequired");
    }
    return QStringLiteral("Idle");
}

UpdateService::State UpdateService::stateFromName(const QString& name)
{
    for (const State state : {Idle, Checking, UpToDate, Available, Downloading, Paused,
                              Verifying, Ready, Installing, RecoverableError,
                              VerificationFailure, ManualUpdateRequired}) {
        if (stateName(state) == name)
            return state;
    }
    return Idle;
}

bool UpdateService::withinRoot(const QString& path) const
{
    const QString root = QDir::cleanPath(m_cacheRoot);
    const QString child = QDir::cleanPath(path);
    return child == root || child.startsWith(root + QLatin1Char('/'));
}

void UpdateService::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emitChanged();
}

void UpdateService::emitChanged()
{
    emit changed();
}

void UpdateService::loadPersisted()
{
    QFile file(QDir(m_cacheRoot).filePath(QStringLiteral("service-state.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        loadSeedState();
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        loadSeedState();
        return;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != kServiceSchemaVersion) {
        loadSeedState();
        return;
    }

    m_seenVersion = object.value(QStringLiteral("seenVersion")).toString();
    m_failedVersion = object.value(QStringLiteral("failedVersion")).toString();
    const QByteArray manifestBytes = QByteArray::fromBase64(
        object.value(QStringLiteral("manifestBase64")).toString().toLatin1());
    const QByteArray signatureBytes = QByteArray::fromHex(
        object.value(QStringLiteral("signatureHex")).toString().toLatin1());
    QString error;
    if (manifestBytes.isEmpty() || signatureBytes.isEmpty()
        || !restoreManifest(manifestBytes, signatureBytes,
                            stateFromName(object.value(QStringLiteral("state")).toString()),
                            object.value(QStringLiteral("seenVersion")).toString().isEmpty()
                                ? false : object.value(QStringLiteral("seenVersion")).toString()
                                      == object.value(QStringLiteral("latestVersion")).toString(),
                            &error)) {
        loadSeedState();
        return;
    }
    m_lastCheckMs = object.value(QStringLiteral("lastCheckMs")).toInteger(-1);
    m_etag = object.value(QStringLiteral("etag")).toString();
    m_receivedBytes = object.value(QStringLiteral("receivedBytes")).toInteger();
    m_totalBytes = object.value(QStringLiteral("totalBytes")).toInteger();
    m_installerPath = object.value(QStringLiteral("installerPath")).toString();
    const QUrl persistedInstallerUrl(object.value(QStringLiteral("installerUrl")).toString());
    if (persistedInstallerUrl.isValid())
        m_assetUrls.insert(m_manifest.installerAsset, persistedInstallerUrl);
    if (m_state == Ready && (m_installerPath.isEmpty() || !QFileInfo(m_installerPath).isFile()))
        m_state = Paused;
    rebuildPresentation();
}

void UpdateService::loadSeedState()
{
    QFile file(QDir(m_cacheRoot).filePath(QStringLiteral("state.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject object = document.object();
    QString manifestPath;
    QString signaturePath;
    if (!safeRelativePath(m_cacheRoot, object.value(QStringLiteral("manifest")).toString(),
                           &manifestPath)
        || !safeRelativePath(m_cacheRoot, object.value(QStringLiteral("signature")).toString(),
                              &signaturePath))
        return;
    QFile manifestFile(manifestPath);
    QFile signatureFile(signaturePath);
    if (!manifestFile.open(QIODevice::ReadOnly) || !signatureFile.open(QIODevice::ReadOnly))
        return;
    const State requested = stateFromName(object.value(QStringLiteral("state")).toString());
    const bool seen = object.value(QStringLiteral("seen")).toBool(false);
    QString error;
    if (!restoreManifest(manifestFile.readAll(), readVerifiedSignature(signatureFile.readAll()),
                         requested, seen, &error))
        return;
    m_seenVersion = seen ? m_latestVersion : QString();
    rebuildPresentation();
}

bool UpdateService::restoreManifest(const QByteArray& manifestBytes,
                                    const QByteArray& signatureBytes, State requestedState,
                                    bool seen, QString* error)
{
    QString verifyError;
    if (!verifyEd25519Raw(manifestBytes, signatureBytes, embeddedUpdatePublicKey(), &verifyError)) {
        fail(error, QStringLiteral("invalid_manifest_signature"));
        return false;
    }
    const auto parsed = parseManifest(manifestBytes, error);
    if (!parsed)
        return false;
    return applyManifest(*parsed, manifestBytes, signatureBytes, requestedState, seen, error);
}

bool UpdateService::applyManifest(const Manifest& manifest, const QByteArray& manifestBytes,
                                  const QByteArray& signatureBytes, State requestedState,
                                  bool seen, QString* error)
{
    if (manifest.schemaVersion != 1 || manifest.version.canonical().isEmpty()) {
        fail(error, QStringLiteral("invalid_manifest"));
        return false;
    }
    m_manifest = manifest;
    m_latestVersion = manifest.version.canonical();
    m_verifiedManifestBytes = manifestBytes;
    m_verifiedSignatureBytes = signatureBytes;
    m_hasChronicle = true;
    if (seen)
        m_seenVersion = m_latestVersion;

    const int installedComparison = manifest.version.compare(m_installedVersion);
    if (manifest.minimumUpdaterVersion.compare(m_installedVersion) > 0) {
        m_updateAvailable = false;
        m_unseenUpdate = false;
        m_state = ManualUpdateRequired;
    } else if (installedComparison <= 0) {
        m_updateAvailable = false;
        m_unseenUpdate = false;
        m_state = UpToDate;
    } else if (m_failedVersion == m_latestVersion) {
        m_updateAvailable = false;
        m_unseenUpdate = false;
        m_state = VerificationFailure;
    } else {
        m_updateAvailable = true;
        m_unseenUpdate = m_seenVersion != m_latestVersion;
        if (requestedState == Ready || requestedState == Installing || requestedState == Paused
            || requestedState == RecoverableError || requestedState == VerificationFailure)
            m_state = requestedState;
        else
            m_state = Available;
    }
    rebuildPresentation();
    return true;
}

void UpdateService::rebuildPresentation()
{
    m_release.clear();
    m_highlights.clear();
    if (!m_hasChronicle)
        return;

    m_release.insert(QStringLiteral("version"), m_manifest.version.canonical());
    m_release.insert(QStringLiteral("tag"), m_manifest.tag);
    m_release.insert(QStringLiteral("eyebrow"), m_manifest.eyebrow);
    m_release.insert(QStringLiteral("title"), m_manifest.title);
    m_release.insert(QStringLiteral("summary"), m_manifest.summary);
    m_release.insert(QStringLiteral("notesUrl"), m_manifest.notesUrl);
    m_release.insert(QStringLiteral("installerAsset"), m_manifest.installerAsset);
    m_release.insert(QStringLiteral("installerSize"), m_manifest.installerSize);
    m_release.insert(QStringLiteral("minimumUpdaterVersion"),
                     m_manifest.minimumUpdaterVersion.canonical());

    const QString artworkRoot = QDir(m_cacheRoot).filePath(QStringLiteral("artwork"));
    QHash<QString, QByteArray> artworkDigests;
    for (const Artwork& artwork : m_manifest.artwork)
        artworkDigests.insert(artwork.assetName, artwork.sha256);
    for (const Highlight& highlight : m_manifest.highlights)
        m_highlights.append(highlightMap(highlight, artworkRoot, artworkDigests));
    emitChanged();
}

void UpdateService::fetchMissingArtwork()
{
    if (!m_hooks.fetchArtwork || m_assetUrls.isEmpty())
        return;
    for (const Artwork& artwork : m_manifest.artwork) {
        QString pathError;
        const QString path = m_cache.artworkPath(artwork.assetName, &pathError);
        if (path.isEmpty())
            continue;
        QFileInfo info(path);
        bool valid = info.isFile() && info.size() >= 0 && info.size() <= kArtworkCapBytes;
        if (valid) {
            QFile file(path);
            valid = file.open(QIODevice::ReadOnly)
                && QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
                       == artwork.sha256;
        }
        if (valid)
            continue;
        const auto url = m_assetUrls.value(artwork.assetName);
        if (!url.isValid())
            continue;
        m_hooks.fetchArtwork(artwork.assetName, url, kArtworkCapBytes,
            [this, assetName = artwork.assetName](const QByteArray& bytes) {
                for (const Artwork& expected : m_manifest.artwork) {
                    if (expected.assetName != assetName)
                        continue;
                    m_cache.writeArtwork(assetName, bytes, expected.sha256, nullptr);
                    rebuildPresentation();
                    return;
                }
            },
            [](const QString&) {});
    }
}

void UpdateService::persist()
{
    if (!m_hasChronicle || m_verifiedManifestBytes.isEmpty() || m_verifiedSignatureBytes.isEmpty())
        return;
    if (!QDir().mkpath(m_cacheRoot))
        return;
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), kServiceSchemaVersion);
    object.insert(QStringLiteral("state"), stateName(m_state));
    object.insert(QStringLiteral("latestVersion"), m_latestVersion);
    object.insert(QStringLiteral("lastCheckMs"), m_lastCheckMs);
    object.insert(QStringLiteral("etag"), m_etag);
    object.insert(QStringLiteral("seenVersion"), m_seenVersion);
    object.insert(QStringLiteral("failedVersion"), m_failedVersion);
    object.insert(QStringLiteral("receivedBytes"), m_receivedBytes);
    object.insert(QStringLiteral("totalBytes"), m_totalBytes);
    object.insert(QStringLiteral("installerPath"), m_installerPath);
    object.insert(QStringLiteral("installerUrl"),
                  m_assetUrls.value(m_manifest.installerAsset).toString());
    object.insert(QStringLiteral("manifestBase64"), QString::fromLatin1(
        m_verifiedManifestBytes.toBase64()));
    object.insert(QStringLiteral("signatureHex"), QString::fromLatin1(
        m_verifiedSignatureBytes.toHex()));
    QSaveFile file(QDir(m_cacheRoot).filePath(QStringLiteral("service-state.json")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void UpdateService::startAutomaticChecks()
{
    if (m_checkInFlight)
        return;
    const qint64 current = nowMs();
    if (m_lastCheckMs >= 0 && current >= m_lastCheckMs
        && current - m_lastCheckMs < kAutomaticCheckIntervalMs)
        return;
    checkNow();
}

void UpdateService::checkNow()
{
    if (m_checkInFlight)
        return;
    if (!m_hooks.checkLatest) {
        if (m_hasChronicle)
            setState(m_state);
        else
            setState(Idle);
        return;
    }
    m_checkInFlight = true;
    m_stateBeforeCheck = m_state;
    m_lastCheckMs = nowMs();
    setState(Checking);
    persist();
    m_hooks.checkLatest(m_etag, [this](ReleaseCheckResult result) {
        handleRelease(std::move(result));
    });
}

void UpdateService::handleRelease(ReleaseCheckResult result)
{
    m_checkInFlight = false;
    if (!result.etag.isEmpty())
        m_etag = result.etag;
    if (result.status == ReleaseCheckResult::Status::NotModified) {
        setState(m_hasChronicle ? m_stateBeforeCheck : UpToDate);
        persist();
        emitChanged();
        return;
    }
    if (result.status != ReleaseCheckResult::Status::Valid) {
        setState(m_hasChronicle ? m_stateBeforeCheck : Idle);
        persist();
        emitChanged();
        return;
    }

    if (m_failedVersion != result.manifest.version.canonical())
        m_failedVersion.clear();
    const bool seen = m_seenVersion == result.manifest.version.canonical();
    QString error;
    bool accepted = false;
    if (!result.verifiedManifestBytes.isEmpty() && !result.verifiedSignatureBytes.isEmpty()) {
        accepted = restoreManifest(result.verifiedManifestBytes, result.verifiedSignatureBytes,
                                   Available, seen, &error);
    } else {
        accepted = applyManifest(result.manifest, {}, {}, Available, seen, &error);
    }
    if (!accepted) {
        setState(m_hasChronicle ? m_stateBeforeCheck : Idle);
        persist();
        return;
    }
    m_etag = result.etag;
    m_assetUrls = result.assetUrls;
    m_installerPath.clear();
    m_receivedBytes = 0;
    m_totalBytes = result.manifest.installerSize;
    persist();
    fetchMissingArtwork();
    emitChanged();
}

void UpdateService::download()
{
    if (!m_hasChronicle || !m_updateAvailable
        || (m_state != Available && m_state != Paused && m_state != RecoverableError))
        return;
    if (!m_hooks.startDownload) {
        handleDownloadFailed(QStringLiteral("download_unavailable"), true);
        return;
    }

    DownloadRequest request;
    request.version = m_manifest.version;
    request.url = m_assetUrls.value(m_manifest.installerAsset);
    request.assetName = m_manifest.installerAsset;
    request.expectedSize = m_manifest.installerSize;
    request.expectedSha256 = m_manifest.installerSha256;
    m_state = Downloading;
    m_receivedBytes = 0;
    m_totalBytes = request.expectedSize;
    emitChanged();
    persist();
    m_hooks.startDownload(request,
        [this](qint64 received, qint64 total, qint64) {
            m_receivedBytes = qMax<qint64>(0, received);
            m_totalBytes = qMax<qint64>(0, total);
            setState(m_totalBytes > 0 && m_receivedBytes >= m_totalBytes ? Verifying
                                                                         : Downloading);
            emitChanged();
            persist();
        },
        [this](const QString& path) {
            m_installerPath = path;
            m_receivedBytes = m_totalBytes;
            setState(Ready);
            persist();
            emitChanged();
        },
        [this](const QString& errorCode, bool resumable) {
            handleDownloadFailed(errorCode, resumable);
        });
}

void UpdateService::handleDownloadFailed(const QString& errorCode, bool resumable)
{
    if (errorCode == QStringLiteral("cancelled")) {
        setState(Paused);
    } else if (errorCode.contains(QStringLiteral("sha256"))
               || errorCode.contains(QStringLiteral("signature"))
               || errorCode == QStringLiteral("verification_failed")) {
        m_failedVersion = m_latestVersion;
        m_updateAvailable = false;
        m_unseenUpdate = false;
        setState(VerificationFailure);
    } else {
        setState(resumable ? Paused : RecoverableError);
    }
    persist();
    emitChanged();
}

void UpdateService::cancelDownload()
{
    if (m_state != Downloading && m_state != Verifying)
        return;
    if (m_hooks.cancelDownload)
        m_hooks.cancelDownload();
    setState(Paused);
    persist();
}

void UpdateService::markSeen()
{
    if (!m_hasChronicle || m_latestVersion.isEmpty())
        return;
    m_seenVersion = m_latestVersion;
    m_unseenUpdate = false;
    persist();
    emitChanged();
}

void UpdateService::restartAndUpdate()
{
    if (m_state != Ready || m_installerPath.isEmpty() || !m_hooks.installLauncher)
        return;
    QString error;
    setState(Installing);
    if (!m_hooks.installLauncher(m_installerPath, m_manifest.version, &error)) {
        setState(RecoverableError);
        persist();
        return;
    }
    persist();
}

} // namespace Colosseum::Update
