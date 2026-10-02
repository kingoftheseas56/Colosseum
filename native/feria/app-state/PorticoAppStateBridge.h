#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

class PorticoAppStateBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList settingsApps READ settingsApps WRITE setSettingsApps NOTIFY settingsAppsChanged)
    Q_PROPERTY(QStringList activeApps READ activeApps NOTIFY activeAppsChanged)
    Q_PROPERTY(QStringList unknownApps READ unknownApps NOTIFY unknownAppsChanged)
    Q_PROPERTY(QObject* runtime READ runtime WRITE setRuntime NOTIFY runtimeChanged)

public:
    explicit PorticoAppStateBridge(QObject *parent = nullptr);

    QStringList settingsApps() const { return m_settingsApps; }
    QStringList activeApps() const { return m_activeApps; }
    QStringList unknownApps() const { return m_unknownApps; }
    QObject *runtime() const { return m_runtime.data(); }

    void setSettingsApps(const QStringList &apps);
    void setRuntime(QObject *runtime);

    Q_INVOKABLE QString backendProviderId(const QString &porticoProviderId) const;
    Q_INVOKABLE bool isKnownProvider(const QString &porticoProviderId) const;
    Q_INVOKABLE bool syncRuntime();

    static QStringList currentPorticoProviderIds();

signals:
    void settingsAppsChanged();
    void activeAppsChanged();
    void unknownAppsChanged();
    void runtimeChanged();

private:
    void recompute();

    QStringList m_settingsApps;
    QStringList m_activeApps;
    QStringList m_unknownApps;
    QPointer<QObject> m_runtime;
    QMetaObject::Connection m_runtimeDestroyed;
};
