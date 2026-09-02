#pragma once

#include <QObject>
#include <QString>

class QProcess;

class PowerStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool inhibited READ inhibited NOTIFY inhibitedChanged)

public:
    explicit PowerStore(QObject *parent = nullptr);
    ~PowerStore() override;

    bool inhibited() const { return m_inhibited; }

    Q_INVOKABLE void setInhibited(bool on, const QString &reason = QString());
    Q_INVOKABLE void release();

signals:
    void inhibitedChanged();

private:
    bool applyPlatformInhibit(bool on, const QString &reason);

    bool m_inhibited = false;
#if defined(Q_OS_LINUX)
    QProcess *m_linuxInhibitor = nullptr;
#endif
};
