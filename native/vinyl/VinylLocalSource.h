#pragma once
#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <atomic>
class QThread;
class VaultIdentity;
// Rebuildable Collection projection, not a second library/identity store.
class VinylLocalSource final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cataloguePageVersion READ cataloguePageVersion CONSTANT)
public:
    // Internal construction seam for isolated native timing qualification; ordinary default is zero.
    explicit VinylLocalSource(VaultIdentity *identity, QObject *parent = nullptr, int resolutionDelayMs = 0);
    ~VinylLocalSource() override;
    Q_INVOKABLE QVariantMap importFiles(const QStringList &pathsOrUrls);
    Q_INVOKABLE int startImport(const QStringList &pathsOrUrls);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE bool decideImportIdentity(const QString &relationship, const QString &choice);
    Q_INVOKABLE void setAlbums(const QVariantList &entries);
    int cataloguePageVersion() const { return 1; }
    Q_INVOKABLE int request(const QString &lane, const QString &operation, const QString &value,
                            const QVariantMap &page = QVariantMap());
    Q_INVOKABLE void cancel(const QString &lane);
    Q_INVOKABLE void cancelAll();
signals:
    void importCompleted(int requestId, const QVariant &result);
    void importProgress(int requestId, int processed, int total, const QString &fileName);
    void completed(const QString &lane, int requestId, const QVariant &data, const QString &error);
    void profileLeaving();
    void profileEntered();
    void checkpointRequested();
    void shuttingDown();
private:
    QVariantMap finishImport(const QVariantMap &inspection);
    QVariantList importManifest(const QString &path, QString *error, QVariantList *ceremonies);
    QVariant read(const QString &operation, const QString &value, QString *error) const;
    VaultIdentity *m_identity;
    QHash<QString, QVariantMap> m_albums, m_tracks, m_artists;
    QHash<QString, int> m_pending;
    const int m_resolutionDelayMs;
    quint64 m_revision = 1;
    int m_serial = 0;
    int m_importRequest = -1;
    std::shared_ptr<std::atomic_bool> m_importCancelled;
    QList<QThread *> m_importWorkers;
};
