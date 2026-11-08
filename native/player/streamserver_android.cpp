#include "streamserver.h"

#ifdef COLOSSEUM_ANDROID_NUVIO_BACKEND

#include "nuviostreambackend.h"

#include <QDebug>

#include <QStandardPaths>

#include <QFile>

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
    // The Android system trust roots must be exported to a private PEM before
    // the native engine is created; certificate verification is never
    // disabled. A failed export leaves the backend unavailable.
    if (!exportAndroidTrustBundle(pemPath)) {
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
            &StreamServer::streamReady
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
            &StreamServer::streamError
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

    }
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
    if (m_foregroundProbe) setForeground(m_foregroundProbe());
    if (!m_backend) {
        // kUnavailableMessage is a constant, not a literal token, so it must
        // not go through QStringLiteral.
        emit streamError(QString::fromLatin1(kUnavailableMessage));
        return;
    }
    m_backend->play(infoHash, fileIdx);
}

void StreamServer::prefetch(const QString &infoHash, int fileIdx) {
    if (m_foregroundProbe) setForeground(m_foregroundProbe());
    if (!m_backend) {
        emit streamError(QString::fromLatin1(kUnavailableMessage));
        return;
    }
    m_backend->prefetch(infoHash, fileIdx);
}

#ifdef Q_OS_ANDROID
void StreamServer::warmSource(const QString &infoHash, int fileIdx) {
    if (m_foregroundProbe) setForeground(m_foregroundProbe());
    if (m_backend) m_backend->warmSource(infoHash, fileIdx);
}
#endif

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

void StreamServer::setBackgroundDownloadsActive(bool active) {
    if (m_backend) m_backend->setBackgroundDownloadsActive(active);
}

void StreamServer::stopFetchDemand() {
    if (m_backend) m_backend->stopPrefetch();
}

#ifdef Q_OS_ANDROID
void StreamServer::stopDemand() {
    if (m_backend) {
        m_backend->stop();
    }
}
#endif

void StreamServer::setForegroundProbe(std::function<bool()> probe) {
    m_foregroundProbe = std::move(probe);
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

void StreamServer::setBackgroundDownloadsActive(bool active)
{
    Q_UNUSED(active)
}

void StreamServer::stopFetchDemand() {}

void StreamServer::setForegroundProbe(std::function<bool()> probe)
{
    m_foregroundProbe = std::move(probe);
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

#ifdef Q_OS_ANDROID
void StreamServer::warmSource(const QString &infoHash, int fileIdx)
{
    prefetch(infoHash, fileIdx);
}
#endif

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

#ifdef Q_OS_ANDROID
void StreamServer::stopDemand()
{
}
#endif

#endif // COLOSSEUM_ANDROID_NUVIO_BACKEND
