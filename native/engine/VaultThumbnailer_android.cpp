#include "VaultThumbnailer.h"

#include "VaultCacheKey.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJniObject>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>

namespace {
constexpr int kMaxInFlight = 3;
constexpr double kOffsetFraction = 0.10;
constexpr double kOffsetFloorSec = 3.0;
constexpr double kFallbackOffsetSec = 60.0;
}

VaultThumbnailer::VaultThumbnailer(QString cacheDir, QObject *parent)
    : QObject(parent), m_cacheDir(std::move(cacheDir))
{
    QDir().mkpath(thumbsDir());
}

VaultThumbnailer::~VaultThumbnailer()
{
    m_pending.clear();
    m_activeKeys.clear();
}

QString VaultThumbnailer::thumbsDir() const
{
    return QDir(m_cacheDir).filePath(QStringLiteral("thumbs"));
}

QString VaultThumbnailer::outputPathForKey(const QString &key) const
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(thumbsDir()).filePath(QString::fromLatin1(hash) + QStringLiteral(".jpg"));
}

double VaultThumbnailer::offsetSecondsForDuration(double durationSec)
{
    if (durationSec <= 0.0)
        return kFallbackOffsetSec;
    double offset = std::max(durationSec * kOffsetFraction, kOffsetFloorSec);
    if (offset >= durationSec)
        offset = durationSec / 2.0;
    return offset;
}

QString VaultThumbnailer::cachedThumbPath(const QString &path, qint64 size, qint64 mtimeMs) const
{
    const QString out = outputPathForKey(VaultCacheKey::make(path, size, mtimeMs));
    return QFileInfo::exists(out) ? out : QString();
}
QString VaultThumbnailer::requestThumb(const QString &path, qint64 size, qint64 mtimeMs,
                                        double knownDurationSec)
{
    const QString key = VaultCacheKey::make(path, size, mtimeMs);
    const QString out = outputPathForKey(key);
    if (QFileInfo::exists(out))
        return out;
    if (m_activeKeys.contains(key))
        return QString();

    const double offset = offsetSecondsForDuration(knownDurationSec);
    m_activeKeys.insert(key);
    if (m_asyncInFlight >= kMaxInFlight) {
        m_pending.enqueue({key, path, offset});
        return QString();
    }
    startJob(key, path, offset);
    return QString();
}

void VaultThumbnailer::startJob(const QString &key, const QString &sourcePath, double offsetSec)
{
    QDir().mkpath(thumbsDir());
    const QString out = outputPathForKey(key);
    const QString tmp = out + QStringLiteral(".tmp");
    QFile::remove(tmp);
    ++m_asyncInFlight;

    QPointer<VaultThumbnailer> weak(this);
    QtConcurrent::run([weak, key, sourcePath, offsetSec, out, tmp]() {
        bool ok = false;
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (context.isValid()) {
            const QJniObject source = QJniObject::fromString(sourcePath);
            const QJniObject output = QJniObject::fromString(tmp);
            ok = QJniObject::callStaticMethod<jboolean>(
                "org/colosseum/vault/VaultMediaProbe", "writeThumbnail",
                "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;J)Z",
                context.object<jobject>(), source.object<jstring>(), output.object<jstring>(),
                jlong(std::max(0.0, offsetSec) * 1000000.0));
        }

        QObject *dispatcher = QCoreApplication::instance();
        if (!dispatcher)
            return;
        QMetaObject::invokeMethod(dispatcher, [weak, key, out, tmp, ok]() {
            if (!weak) {
                QFile::remove(tmp);
                return;
            }
            weak->m_asyncInFlight = std::max(0, weak->m_asyncInFlight - 1);
            weak->m_activeKeys.remove(key);
            bool promoted = false;
            if (ok && QFileInfo::exists(tmp) && QFileInfo(tmp).size() > 0) {
                QFile::remove(out);
                promoted = QFile::rename(tmp, out);
            }
            if (!promoted)
                QFile::remove(tmp);
            else
                emit weak->thumbReady(key, out);
            weak->pumpQueue();
        }, Qt::QueuedConnection);
    });
}

void VaultThumbnailer::pumpQueue()
{
    while (!m_pending.isEmpty() && m_asyncInFlight < kMaxInFlight) {
        const PendingRequest request = m_pending.dequeue();
        const QString out = outputPathForKey(request.key);
        if (QFileInfo::exists(out)) {
            m_activeKeys.remove(request.key);
            continue;
        }
        startJob(request.key, request.sourcePath, request.offsetSec);
    }
}

void VaultThumbnailer::finishJob(QProcess *proc, int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(proc)
    Q_UNUSED(exitCode)
    Q_UNUSED(status)
}

void VaultThumbnailer::killJob(QProcess *proc)
{
    Q_UNUSED(proc)
}
