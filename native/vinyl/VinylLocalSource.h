#pragma once
#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
class VaultIdentity;
// Rebuildable Collection projection, not a second library/identity store.
class VinylLocalSource final : public QObject {
    Q_OBJECT
public:
    explicit VinylLocalSource(VaultIdentity *identity, QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap importFiles(const QStringList &pathsOrUrls);
    Q_INVOKABLE void setAlbums(const QVariantList &entries);
    Q_INVOKABLE int request(const QString &lane, const QString &operation, const QString &value);
    Q_INVOKABLE void cancel(const QString &lane);
    Q_INVOKABLE void cancelAll();
signals:
    void completed(const QString &lane, int requestId, const QVariant &data, const QString &error);
    void profileLeaving();
    void profileEntered();
    void checkpointRequested();
    void shuttingDown();
private:
    QVariant read(const QString &operation, const QString &value, QString *error) const;
    VaultIdentity *m_identity;
    QHash<QString, QVariantMap> m_albums, m_tracks;
    QHash<QString, int> m_pending;
    int m_serial = 0;
};
