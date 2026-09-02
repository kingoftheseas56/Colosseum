#include "powerstore.h"

#include <QProcess>

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
#if defined(Q_OS_WIN)
    Q_UNUSED(reason)
    EXECUTION_STATE state = ES_CONTINUOUS;
    if (on)
        state |= ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED;
    return SetThreadExecutionState(state) != 0;
#elif defined(Q_OS_LINUX)
    if (on) {
        if (m_linuxInhibitor && m_linuxInhibitor->state() != QProcess::NotRunning)
            return true;
        if (!m_linuxInhibitor)
            m_linuxInhibitor = new QProcess(this);

        const QString why = reason.trimmed().isEmpty()
            ? QStringLiteral("Colosseum playback")
            : reason.trimmed();
        m_linuxInhibitor->setProgram(QStringLiteral("systemd-inhibit"));
        m_linuxInhibitor->setArguments({
            QStringLiteral("--what=idle:sleep"),
            QStringLiteral("--who=Colosseum"),
            QStringLiteral("--why=") + why,
            QStringLiteral("--mode=block"),
            QStringLiteral("sleep"),
            QStringLiteral("infinity")
        });
        m_linuxInhibitor->start();
        return m_linuxInhibitor->waitForStarted(1500);
    }

    if (!m_linuxInhibitor || m_linuxInhibitor->state() == QProcess::NotRunning)
        return true;
    m_linuxInhibitor->terminate();
    if (!m_linuxInhibitor->waitForFinished(1000)) {
        m_linuxInhibitor->kill();
        m_linuxInhibitor->waitForFinished(1000);
    }
    return m_linuxInhibitor->state() == QProcess::NotRunning;
#else
    Q_UNUSED(on)
    Q_UNUSED(reason)
    return true;
#endif
}
