#include "bootstrap/InstanceLock.h"

#include <QDir>
#include <QFileInfo>

ColosseumInstanceLock::ColosseumInstanceLock(const QString& appDataRoot)
    : m_path(appDataRoot.trimmed().isEmpty()
                 ? QString()
                 : QDir(appDataRoot).filePath(QStringLiteral("colosseum.instance.lock"))),
      m_lock(m_path)
{
    // QLockFile checks whether the recorded process is still alive before
    // treating a lock as stale. The timeout also lets a crashed process leave
    // the lock recoverable on platforms where that process check is limited.
    m_lock.setStaleLockTime(30 * 1000);
}

bool ColosseumInstanceLock::tryAcquire()
{
    if (m_path.isEmpty())
        return false;

    const QFileInfo lockInfo(m_path);
    if (!QDir().mkpath(lockInfo.absolutePath()))
        return false;

    return m_lock.tryLock(0);
}

bool ColosseumInstanceLock::wasBlockedByExistingInstance() const
{
    return m_lock.error() == QLockFile::LockFailedError;
}
