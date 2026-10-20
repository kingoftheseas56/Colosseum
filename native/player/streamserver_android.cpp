#include "streamserver.h"

#ifdef COLOSSEUM_ANDROID_NUVIO_BACKEND

#include "nuviostreambackend.h"

#include <QDebug>
#include <QCryptographicHash>

#include <QStandardPaths>

#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include <QDir>
#include <QFileInfo>

namespace {

constexpr auto kUnavailableMessage = "Colosseum Server is not available: the nuvio engine library is missing";

QString androidContextNativeLibraryDir() {
#ifdef Q_OS_ANDROID
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return {};
    }
    const QJniObject contextObject = context;
    const auto info = contextObject.callObjectMethod(
        "getApplicationInfo",
        "()Landroid/content/pm/ApplicationInfo;"
    );
    if (!info.isValid()) {
        return {};
    }
    const auto dir = info.getObjectField("nativeLibraryDir", "Ljava/lang/String;");
    return dir.toString();
#else
    return {};
#endif
}

QString androidCurrentPackage() {
#ifdef Q_OS_ANDROID
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return {};
    const QJniObject obj = context;
    return obj.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
#else
    return {};
#endif
}

bool exportAndroidTrustBundle(const QString &pemPath) {
#ifdef Q_OS_ANDROID
    QDir().mkpath(QFileInfo(pemPath).absolutePath());
    return QJniObject::callStaticMethod<jboolean>(
        "org/colosseum/player/NuvioAndroidTrust",
        "exportTo",
        "(Ljava/lang/String;)Z",
        QJniObject::fromString(pemPath).object<jstring>()
    );
#else
    Q_UNUSED(pemPath)
    return true;
#endif
}

} // namespace

StreamServer::StreamServer(QObject *parent)
    : QObject(parent) {
    const auto appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/nuvio");
    const auto pemPath = appData + QStringLiteral("/tls/android-ca.pem");

    Colosseum::Nuvio::NuvioStreamBackend::Configuration configuration;
    configuration.dataDirectory = appData + QStringLiteral("/data");
    configuration.cacheDirectory =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/nuvio");
    configuration.tlsCaBundlePath = pemPath;
    // The fixture and diagnostic APIs exist only in this explicitly named,
    // independently signed debug package. Production packages keep defaults.
#ifdef COLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE
    m_diagnosticTask =
        androidCurrentPackage() == QStringLiteral(COLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE);
#else
    m_diagnosticTask = false;
#endif
    bool taskDirectoriesReady = true;
    if (m_diagnosticTask) {
        qputenv("NUVIO_ENGINE_DISABLE_DISCOVERY", "1");
        configuration.requireFixtureIsolation = true;
        configuration.dataDirectory = appData + QStringLiteral("/diag013-data");
        configuration.cacheDirectory =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
            QStringLiteral("/nuvio/diag013-cache");
        taskDirectoriesReady = QDir().mkpath(configuration.dataDirectory) &&
                               QDir().mkpath(configuration.cacheDirectory);
        qInfo() << "[nuvio011] diagnostic package, isolated fresh payload/cache roots"
                << configuration.dataDirectory << configuration.cacheDirectory
                << "created" << taskDirectoriesReady;
    }

    // The Android system trust roots must be exported to a private PEM before
    // the native engine is created; certificate verification is never
    // disabled. A failed export leaves the backend unavailable.
    if (!taskDirectoriesReady || !exportAndroidTrustBundle(pemPath)) {
        m_engineUnavailable = true;
        m_backend.reset(); // no engine without trust roots
    } else {
        const QString engineLibraryPath =
            androidContextNativeLibraryDir() + QStringLiteral("/libnuvio_engine.so");
        m_backend = std::make_unique<Colosseum::Nuvio::NuvioStreamBackend>(
            engineLibraryPath,
            configuration,
            this
        );
        // Task010-correction evidence: the eager loadEngineApi outcome must be
        // observable (resolved path + API-table availability) without a session.
        qInfo() << QStringLiteral("[nuvio] startup engine path: %1, api available: %2")
                       .arg(engineLibraryPath,
                            m_backend->available() ? QStringLiteral("true")
                                                   : QStringLiteral("false"));
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::readyChanged,
            this,
            &StreamServer::readyChanged
        );
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::startingChanged,
            this,
            &StreamServer::startingChanged
        );
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::engineUnavailableChanged,
            this,
            &StreamServer::engineUnavailableChanged
        );
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::streamReady,
            this,
            [this](const QString &url, const QString &infoHash, int fileIdx) {
                // Task-only: the loopback route target embeds an unguessable
                // stream token, so external Range verification of the same
                // Nuvio URL needs the exact route identity logged here. The
                // diagnostic package alone logs it; production stays silent.
                if (m_diagnosticTask) {
                    qInfo() << "[nuvio011] streamReady url:" << url
                            << "infoHash:" << infoHash << "fileIdx:" << fileIdx;
                }
                emit streamReady(url, infoHash, fileIdx);
            }
        );
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::fetchReady,
            this,
            &StreamServer::fetchReady
        );
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::streamError,
            this,
            [this](const QString &message) {
                if (m_diagnosticTask)
                    qWarning() << "[nuvio013] streamError:" << message;
                emit streamError(message);
            }
        );
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::streamStats,
            this,
            &StreamServer::streamStats
        );
        // Runtime engine-unavailability (an engine create failure after a
        // successful library load) must be reflected by the facade property
        // and its notification, not merely forwarded as an unrelated signal.
        connect(
            m_backend.get(),
            &Colosseum::Nuvio::NuvioStreamBackend::engineUnavailableChanged,
            this,
            [this] {
                setEngineUnavailable(m_backend && m_backend->engineUnavailable());
            }
        );
        setEngineUnavailable(!m_backend->available());
        if (m_diagnosticTask) {
        // Task-only fixture trigger: watch the app-private files directory
        // for nuvio013-trigger.json (written over ADB via run-as). Only this
        // task-owned file ever drives the fixture seams.
        const QString triggerDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(triggerDir);
        m_triggerWatcher = new QFileSystemWatcher(QStringList(triggerDir), this);
        m_triggerDebounce = new QTimer(this);
        m_triggerDebounce->setSingleShot(true);
        m_triggerDebounce->setInterval(600);
        connect(m_triggerWatcher, &QFileSystemWatcher::directoryChanged,
                this, [this] { m_triggerDebounce->start(); });
        connect(m_triggerDebounce, &QTimer::timeout, this, &StreamServer::handleTriggerFile);
        }
    }
}

// Task 013 synthetic fixture (cloud-handoff/android-013/claude-013/fixture013):
// the only metadata the diagnostic package admits, pinned by identity and bytes.
QByteArray StreamServer::loadPinnedFixture(const QString &infoHash, int fileIdx) const {
    const QString sourcePath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/nuvio013-fixture.torrent");
    QFile torrentFile(sourcePath);
    const auto expectedTorrentSha = QByteArrayLiteral(
        "0a739d0665cd732d8391ab827cd05ba42af373599c0ba7234ac86a85d7e9a9e0");
    if (infoHash != QStringLiteral("7f13e4dfe42445a675cccb6fda1e7549babad2a8") ||
        fileIdx != 0 || !torrentFile.open(QIODevice::ReadOnly) ||
        torrentFile.size() != 2262) {
        qWarning() << "[nuvio011] fixture trigger rejected: identity or metadata unavailable";
        return {};
    }
    const auto contents = torrentFile.readAll();
    if (QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex() !=
        expectedTorrentSha) {
        qWarning() << "[nuvio011] fixture trigger rejected: torrent hash mismatch";
        return {};
    }
    qInfo() << "[nuvio011] verified private torrent metadata:" << infoHash
            << "bytes" << contents.size();
    return contents;
}

void StreamServer::handleTriggerFile() {
    if (!m_diagnosticTask) return;
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/nuvio013-trigger.json");
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    const QString op = object.value(QStringLiteral("op")).toString();
    if (op == QLatin1String("verify")) {
        qInfo() << "[nuvio] trigger: verify received";
        if (m_backend) {
            m_backend->verifyIsolation();
        }
        return;
    }
    if (op == QLatin1String("play")) {
        const QString infoHash = object.value(QStringLiteral("infoHash")).toString();
        const int fileIdx = object.value(QStringLiteral("fileIdx")).toInt(-1);
        const QString title =
            object.value(QStringLiteral("title")).toString(QStringLiteral("Nuvio 013 fixture"));
        const auto contents = loadPinnedFixture(infoHash, fileIdx);
        if (contents.isEmpty())
            return;
        m_pendingFixtureTorrentData = contents;
        m_pendingFixtureInfoHash = infoHash;
        emit fixturePlayRequested(infoHash, fileIdx, title);
        return;
    }
    qWarning() << "[nuvio] trigger: unknown op";
}

StreamServer::~StreamServer() = default;

void StreamServer::setEngineUnavailable(bool unavailable) {
    if (m_engineUnavailable == unavailable) {
        return;
    }
    m_engineUnavailable = unavailable;
    emit engineUnavailableChanged();
}

bool StreamServer::ready() const {
    return m_backend && m_backend->ready();
}

bool StreamServer::starting() const {
    return m_backend && m_backend->starting();
}

void StreamServer::play(const QString &infoHash, int fileIdx) {
    if (!m_backend) {
        // kUnavailableMessage is a constant, not a literal token, so it must
        // not go through QStringLiteral.
        emit streamError(QString::fromLatin1(kUnavailableMessage));
        return;
    }
    // Fixed, hash-pinned private .torrent metadata is routed through the
    // application's existing Stream demand rather than a direct player URL.
    if (m_diagnosticTask && !m_pendingFixtureTorrentData.isEmpty() &&
        m_pendingFixtureInfoHash == infoHash) {
        const QByteArray metadata = std::exchange(m_pendingFixtureTorrentData, QByteArray());
        m_pendingFixtureInfoHash.clear();
        m_backend->playTorrentData(metadata, infoHash, fileIdx);
        return;
    }
    if (m_diagnosticTask) {
        // A fixture re-demand (foreground return after the background quiesce
        // destroyed the session): prove isolation on the new session, then
        // admit the same pinned metadata. The worker runs both in order.
        const auto metadata = loadPinnedFixture(infoHash, fileIdx);
        if (!metadata.isEmpty()) {
            qInfo() << "[nuvio013] fixture re-demand: verify then admit on the current session";
            m_backend->verifyIsolation();
            m_backend->playTorrentData(metadata, infoHash, fileIdx);
            return;
        }
    }
    m_backend->play(infoHash, fileIdx);
}

void StreamServer::prefetch(const QString &infoHash, int fileIdx) {
    if (!m_backend) {
        emit streamError(QString::fromLatin1(kUnavailableMessage));
        return;
    }
    m_backend->prefetch(infoHash, fileIdx);
}

void StreamServer::warmUp() {
    // Deliberately lazy: never starts torrent networking at app startup.
}

QString StreamServer::streamUrl(const QString &infoHash, int fileIdx) const {
    return m_backend ? m_backend->streamUrl(infoHash, fileIdx) : QString();
}

void StreamServer::watchStats(const QString &infoHash, int fileIdx) {
    if (m_backend) {
        m_backend->watchStats(infoHash, fileIdx);
    }
}

void StreamServer::unwatchStats() {
    if (m_backend) {
        m_backend->unwatchStats();
    }
}

void StreamServer::setForeground(bool foreground) {
    if (m_backend) {
        m_backend->setForeground(foreground);
    }
}

void StreamServer::stopDemand() {
    if (m_backend) {
        m_backend->stop();
    }
}

#else // stub: Android streaming unavailable

namespace {
const QString kUnavailableMessage = QStringLiteral(
    "Colosseum Server is not available on Android yet");
}

StreamServer::StreamServer(QObject *parent)
    : QObject(parent)
{
    m_engineUnavailable = true;
}

StreamServer::~StreamServer() = default;

bool StreamServer::ready() const
{
    return false;
}

bool StreamServer::starting() const
{
    return false;
}

void StreamServer::setForeground(bool foreground)
{
    Q_UNUSED(foreground)
}

void StreamServer::setEngineUnavailable(bool unavailable)
{
    if (m_engineUnavailable == unavailable)
        return;
    m_engineUnavailable = unavailable;
    emit engineUnavailableChanged();
}

void StreamServer::play(const QString &infoHash, int fileIdx)
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
    emit streamError(kUnavailableMessage);
}

void StreamServer::prefetch(const QString &infoHash, int fileIdx)
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
    emit streamError(kUnavailableMessage);
}

void StreamServer::warmUp()
{
    emit streamError(kUnavailableMessage);
}

QString StreamServer::streamUrl(const QString &infoHash, int fileIdx) const
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
    return {};
}

void StreamServer::watchStats(const QString &infoHash, int fileIdx)
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
}

void StreamServer::unwatchStats()
{
}

void StreamServer::stopDemand()
{
}

#endif // COLOSSEUM_ANDROID_NUVIO_BACKEND
