#include "powerstore.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

PowerStore::PowerStore(QObject *parent)
    : QObject(parent) {}

PowerStore::~PowerStore() {
    release();
}

void PowerStore::setInhibited(bool on, const QString &reason) {
    if (m_inhibited == on)
        return;
    if (!applyPlatformInhibit(on, reason))
        return;
    m_inhibited = on;
    emit inhibitedChanged();
}

void PowerStore::release() {
    setInhibited(false, QString());
}

bool PowerStore::applyPlatformInhibit(bool on, const QString &reason) {
    Q_UNUSED(reason)
#if defined(Q_OS_WIN)
    EXECUTION_STATE state = ES_CONTINUOUS;
    if (on)
        state |= ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED;
    return SetThreadExecutionState(state) != 0;
#else
    Q_UNUSED(on)
    return true;
#endif
}
