#include "BackgroundDownloadBridge.h"

#include "BackgroundDownloadPolicy.h"
#include "engine/LocalDownloads.h"
#include <QGuiApplication>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <functional>

namespace {
QMutex downloadBridgeMutex;
QHash<qint64, QPointer<Colosseum::Platform::BackgroundDownloadBridge>> downloadBridges;
std::atomic<qint64> nextDownloadBridgeHandle{1};

void postToDownloadBridge(qint64 handle,
    std::function<void(Colosseum::Platform::BackgroundDownloadBridge *)> callback) {
    QMutexLocker lock(&downloadBridgeMutex);
    const auto bridge = downloadBridges.value(handle);
    if (bridge) QMetaObject::invokeMethod(bridge, [bridge, callback = std::move(callback)] {
        if (bridge) callback(bridge);
    }, Qt::QueuedConnection);
}
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_platform_DownloadService_nativeHostState(
    JNIEnv *env, jclass, jlong handle, jlong epoch, jboolean ready, jstring error) {
    Q_UNUSED(env)
    const QString reason = QJniObject(error).toString();
    postToDownloadBridge(handle, [epoch, ready, reason](auto *bridge) {
        bridge->acceptHostState(epoch, ready, reason);
    });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_platform_DownloadService_nativeOpenDownloads(JNIEnv *, jclass, jlong handle) {
    postToDownloadBridge(handle, [](auto *bridge) { emit bridge->openDownloadsRequested(); });
}
#endif

namespace Colosseum::Platform {

BackgroundDownloadBridge::BackgroundDownloadBridge(QObject *parent)
    : QObject(parent) {
#if defined(Q_OS_ANDROID)
    m_nativeHandle = nextDownloadBridgeHandle.fetch_add(1);
    QMutexLocker lock(&downloadBridgeMutex);
    downloadBridges.insert(m_nativeHandle, this);
#endif
}

BackgroundDownloadBridge::BackgroundDownloadBridge(
    LocalDownloads *source, QObject *parent)
    : BackgroundDownloadBridge(parent) {
    setSource(source);
}

BackgroundDownloadBridge::~BackgroundDownloadBridge() {
#if defined(Q_OS_ANDROID)
    m_jobs.clear();
    m_runningCount = 0;
    publishAndroidSnapshot();
    QMutexLocker lock(&downloadBridgeMutex);
    downloadBridges.remove(m_nativeHandle);
#endif
}

void BackgroundDownloadBridge::setForeground(bool foreground) {
    if (m_foreground == foreground) return;
    m_foreground = foreground;
    publishAndroidSnapshot();
}

void BackgroundDownloadBridge::acceptHostState(qint64 epoch, bool ready, const QString &error) {
    if (epoch != m_hostEpoch) return;
    ready = ready && backgroundHostRequired();
    if (m_hostReady == ready && m_hostError == error) return;
    m_hostReady = ready;
    m_hostError = error;
    emit hostStateChanged();
}

void BackgroundDownloadBridge::publishAndroidSnapshot() {
#if defined(Q_OS_ANDROID)
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return;
    QString title;
    qint64 received = 0;
    qint64 total = 0;
    bool totalKnown = true;
    for (const QVariant &value : m_jobs) {
        const QVariantMap job = value.toMap();
        if (!jobRequiresBackgroundHost(job)) continue;
        if (title.isEmpty()) title = job.value(QStringLiteral("title")).toString();
        received += qMax<qint64>(0, job.value(QStringLiteral("received")).toLongLong());
        const qint64 expected = job.value(QStringLiteral("total")).toLongLong();
        if (expected <= 0) totalKnown = false;
        else total += expected;
    }
    const auto javaTitle = QJniObject::fromString(title);
    QJniObject::callStaticMethod<void>("org/colosseum/platform/DownloadService", "update",
        "(Landroid/content/Context;JJILjava/lang/String;JJZ)V",
        context.object<jobject>(), jlong(m_nativeHandle), jlong(m_hostEpoch), jint(m_runningCount),
        javaTitle.object<jstring>(), jlong(received), jlong(totalKnown ? total : 0), jboolean(m_foreground));
#endif
}

QVariantList BackgroundDownloadBridge::jobs() const {
    return m_jobs;
}

int BackgroundDownloadBridge::runningCount() const {
    return m_runningCount;
}

bool BackgroundDownloadBridge::backgroundHostRequired() const {
    return m_runningCount > 0;
}

void BackgroundDownloadBridge::setSource(LocalDownloads *source) {
    if (m_source == source)
        return;

    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);

    m_source = source;
    if (m_source) {
        connect(m_source, &LocalDownloads::changed,
                this, &BackgroundDownloadBridge::refresh);
    }
    refresh();
}

void BackgroundDownloadBridge::refresh() {
    const QVariantList nextJobs = m_source ? m_source->activeJobs() : QVariantList();
    int nextRunningCount = 0;
    int nextVideoRunningCount = 0;
    for (const QVariant &value : nextJobs) {
        const QVariantMap job = value.toMap();
        if (jobRequiresBackgroundHost(job)) {
            ++nextRunningCount;
            if (job.value(QStringLiteral("world")).toString() == QStringLiteral("theatre"))
                ++nextVideoRunningCount;
        }
    }

    if (nextJobs == m_jobs && nextRunningCount == m_runningCount)
        return;

    if ((m_runningCount > 0) != (nextRunningCount > 0)) {
        ++m_hostEpoch;
        m_hostReady = false;
        if (nextRunningCount == 0) m_hostError.clear();
        emit hostStateChanged();
    }
    m_jobs = nextJobs;
    m_runningCount = nextRunningCount;
    m_videoRunningCount = nextVideoRunningCount;
    emit changed();
    emit platformSnapshotReady(m_jobs);
    publishAndroidSnapshot();
}

} // namespace Colosseum::Platform
