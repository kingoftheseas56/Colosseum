#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;

// Downloads are data until verified against the embedded addon trust root.
// This store never executes code and exposes only the two fixed Spanish addons.
class TankoyomiAddonStore final : public QObject
{
public:
    using Completion = std::function<void(QByteArray, QStringList, QString)>;
    struct VerifiedAddon {
        QByteArray script;
        QStringList allowedHosts;
        qint64 revision = 0;
    };
    explicit TankoyomiAddonStore(QUrl workerBase, QObject *parent = nullptr);
    // Explicit dependency injection for fixtures; production uses the ctor above.
    TankoyomiAddonStore(QUrl workerBase, QByteArray publicKey, QString cacheRoot,
                       QNetworkAccessManager *network, QObject *parent = nullptr);
    void load(const QString &providerId, Completion completion);

    static bool verifyEnvelope(const QByteArray &envelope, const QString &providerId,
                               const QByteArray &publicKey, VerifiedAddon *addon, QString *error);
    static QByteArray productionPublicKey();

private:
    QUrl m_workerBase;
    QByteArray m_publicKey;
    QString m_cacheRoot;
    QPointer<QNetworkAccessManager> m_network;
};
