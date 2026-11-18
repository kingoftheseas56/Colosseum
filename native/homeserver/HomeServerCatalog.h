#pragma once
#include "HomeServerClient.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

namespace HomeServerCatalog {
QJsonObject normalizeJellyfinItem(const QJsonObject &raw, const QJsonObject &library);
QJsonObject normalizePlexItem(const QJsonObject &raw, const QJsonObject &library);
QJsonArray inheritSeriesIdentity(QJsonArray items);
bool identitiesMatch(const QJsonObject &a, const QJsonObject &b);
QString canonicalVideoId(const QJsonObject &item);
QVariantList matchingSources(QJsonArray items, QVariantMap identity, QJsonObject connection);
}

class HomeServerRefresh final : public QObject {
public:
    struct Result {
        QJsonArray items;
        QString error;
        bool expired = false;
        int serverMajor = 0;
        QJsonArray libraries;
        bool ok() const { return error.isEmpty(); }
    };
    using Callback = std::function<void(Result)>;
    explicit HomeServerRefresh(HomeServerClient *client, QObject *parent = nullptr);
    ~HomeServerRefresh() override;
    void start(QJsonObject connection, QString token, Callback callback);
    void cancel();
private:
    void request(const QString &path, bool publicInfo, std::function<void(QJsonObject)> callback);
    void libraries();
    void page();
    void consumePage(const QJsonObject &body);
    void finish(QString error = {}, bool expired = false);
    HomeServerClient *m_client;
    Callback m_callback;
    QJsonObject m_connection;
    QString m_token;
    QJsonArray m_libraries, m_items;
    QSet<QString> m_ids;
    QHash<QString,QJsonObject> m_buffered;
    QHash<QString,QJsonArray> m_libraryItems;
    qint64 m_snapshotBytes = 0;
    int m_library = 0, m_stage = 0, m_start = 0, m_total = -1, m_pages = 0, m_major = 0;
    bool m_started = false, m_finished = false;
};
