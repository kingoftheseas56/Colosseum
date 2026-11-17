#pragma once
// Local-first read-only audiobook catalogue. No audio, profiles or full-data downloads.
#include "CatalogRemote.h"
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QUrl;

class AudiobookCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool remoteEnabled READ remoteEnabled NOTIFY revisionChanged)
public:
    explicit AudiobookCatalog(const QString& sqlitePath = {}, QObject* parent = nullptr);
    ~AudiobookCatalog() override;
    Q_INVOKABLE bool ready() const { return m_ok; }
    Q_INVOKABLE bool available() const { return m_ok || m_remote->available(); }
    bool remoteEnabled() const { return m_remote->enabled(); }
    int revision() const { return m_remote->revision(); }
    Q_INVOKABLE bool remotePending() const { return m_remote->pending(); }
    Q_INVOKABLE QString remoteQueryKey(const QString& method, const QVariantList& args) const {
        return m_remote->queryKey(method,args);
    }
    Q_INVOKABLE bool reopen(const QString& sqlitePath);
    Q_INVOKABLE QVariantList search(const QString& text, int limit = 24) const;
    Q_INVOKABLE QVariantMap work(const QString& id) const;
    Q_INVOKABLE QVariantMap recordingByAsin(const QString& asin) const;
    Q_INVOKABLE QVariantMap recordingByIsbn(const QString& isbn) const;
    Q_INVOKABLE QVariantMap series(const QString& id) const;
    Q_INVOKABLE QVariantList latest(int page = 0) const;
    Q_INVOKABLE QVariantList byPerson(const QString& personId, int page = 0) const;
    Q_INVOKABLE QVariantList shelves() const;
    Q_INVOKABLE QVariantMap shelf(const QString& key, int page = 0) const;
    Q_INVOKABLE QVariantList genres() const;
    Q_INVOKABLE QVariantMap genre(const QString& name, int page = 0) const;
    void configureRemote(QNetworkAccessManager* manager, const QUrl& endpoint,
                         const QString& cachePath = {});
signals:
    void revisionChanged();
    void readyChanged();
    void requestFailed(QString method, QString reason);
    void remoteQueryChanged(QString key);
private:
    bool open(const QString& sqlitePath);
    QVariantList select(const QString& statement, const QVariantList& bind = {}) const;
    QVariantMap localRecording(const QVariantMap& row) const;
    QVariantMap localItem(const QVariantMap& row) const;
    QVariantMap localPage(const QString& condition, const QVariantList& binds, int page) const;
    QVariantMap recordingBy(const QString& kind, const QString& key) const;
    QSqlDatabase m_db;
    QString m_connection;
    CatalogRemote* m_remote = nullptr;
    bool m_ok = false;
};
