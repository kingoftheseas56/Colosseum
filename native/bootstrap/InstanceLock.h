#pragma once

#include <QLockFile>
#include <QString>

// Guards one normal Colosseum AppDataLocation from concurrent desktop launches.
// Test launches that use a different AppDataLocation (for example, via
// COLOSSEUM_APPDATA_TAG) intentionally receive a separate lock.
class ColosseumInstanceLock
{
public:
    explicit ColosseumInstanceLock(const QString& appDataRoot);

    ColosseumInstanceLock(const ColosseumInstanceLock&) = delete;
    ColosseumInstanceLock& operator=(const ColosseumInstanceLock&) = delete;

    bool tryAcquire();
    bool isLocked() const { return m_lock.isLocked(); }
    bool wasBlockedByExistingInstance() const;

private:
    QString m_path;
    QLockFile m_lock;
};
