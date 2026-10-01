#pragma once

#include <QObject>
#include <QString>

class MalConnectionController;
class SimklConnectionController;

class TrackerConnectionRouter final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool moveAvailable READ moveAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(QString activeProviderKey READ activeProviderKey NOTIFY stateChanged)

public:
    TrackerConnectionRouter(SimklConnectionController *simkl,
                            MalConnectionController *mal,
                            QObject *parent = nullptr);

    bool available() const;
    bool busy() const;
    bool moveAvailable() const;
    QString phase() const;
    QString userCode() const;
    QString verificationUrl() const;
    QString statusMessage() const;
    QString activeProviderKey() const { return m_activeProviderKey; }

    Q_INVOKABLE bool beginConnection(const QString &providerKey);
    Q_INVOKABLE bool openApprovalPage();
    Q_INVOKABLE bool moveConnectionToThisProfile();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismiss();

    bool prepareForProfileDeactivation();

signals:
    void stateChanged();
    void connectionEstablished(const QString &providerKey);

private:
    bool malActive() const
    {
        return m_activeProviderKey == QLatin1String("mal");
    }

    SimklConnectionController *m_simkl = nullptr;
    MalConnectionController *m_mal = nullptr;
    QString m_activeProviderKey = QStringLiteral("simkl");
};
