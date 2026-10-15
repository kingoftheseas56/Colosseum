#pragma once
#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QJsonValue>
#include <QPointer>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <functional>
class QNetworkAccessManager;
class QNetworkReply;
class StaticCatalogueResolver;
// Per-query semantic cache. A configured service is not a complete local database.
class CatalogRemote final : public QObject {
    Q_OBJECT
public:
    explicit CatalogRemote(QString dataset, QObject* parent = nullptr);
    ~CatalogRemote() override;
    // Set before configure; the validator must not mutate the consumer generation.
    void setResultValidator(std::function<bool(const QString&, const QJsonValue&)> validator);
    void configure(QNetworkAccessManager*, const QUrl&, const QString& cachePath);
    bool enabled() const;
    bool available() const;
    int revision() const { return m_revision; }
    QVariant read(const QString& method, const QVariantList& args, const QVariant& empty, bool force = false);
    // Read-only snapshot for the exact method+args key used by read().
    // status: missing | pending | success | failure; cached is independent of status.
    QVariantMap queryState(const QString& method, const QVariantList& args, const QVariant& empty) const;
    // Explicit retry ONLY for this failed key; respects in-flight dedupe and request cap.
    bool retry(const QString& method, const QVariantList& args, const QVariant& empty);
    bool pending() const { return !m_pending.isEmpty(); }
    bool waiting() const;
signals:
    void changed();
    void failed(QString method, QString reason);
    void queryChanged(QString key);
private:
    struct Entry { QString method; QJsonValue result; QDateTime fetched; QString revision; QString etag; };
    struct Query { QString method; QVariantList args; QVariant empty; bool renewed = false; bool dispatched = false; QString conditionalEtag;
        quint64 staticJob = 0, attempt = 0; bool staticChecked = false; int restarts = 0; QString expectedSource; };
    struct Failure { QDateTime when; QString reason; };
    QString key(const QString&, const QVariantList&) const;
    void dispatch(const QString&);
    void tryStatic(const QString&);
    void adoptPublication(const QString&);
    void complete(const QString&, const QJsonValue&, const QString&, const QString& etag = {});
    bool queue(const QString&, const QString&, const QVariantList&, const QVariant&, bool bypassCooldown);
    void lease();
    void observe(QNetworkReply*, const QString&, bool session);
    void finishFailure(const QString&, const QString&);
    void load();
    void save();
    void bound();
    std::function<bool(const QString&, const QJsonValue&)> m_validator;
    QString m_dataset, m_path, m_sourceRevision, m_publication;
    QPointer<StaticCatalogueResolver> m_static;
    QMetaObject::Connection m_staticConnection;
    quint64 m_sourceGeneration = 0;
    QUrl m_endpoint;
    QPointer<QNetworkAccessManager> m_nam;
    QHash<QString, Entry> m_cache;
    QHash<QString, Query> m_pending;
    QHash<QString, Failure> m_failures;
    QList<QPointer<QNetworkReply>> m_replies;
    QByteArray m_token;
    bool m_leasing = false;
    quint64 m_generation = 0;
    int m_revision = 0;
};
