#pragma once
// TankobanCatalog — read-only seam onto the baked Tankoban volume catalogue
// (data/tankoban_catalog.db, built by scripts/manga_brain/build_tankoban_catalog.py
// from the arc's spine.jsonl, plus an optional BookWalker/mymangaindex cover
// harvest). Catalogue-independence Slice 1 (2026-08-20): the offline,
// provider-free source of per-series volume counts and covers — the data
// spine every later slice reads. MalCatalog pattern exactly: QML paints, C++
// decides; db/schema/query shape live here. Missing db => ready()==false and
// every accessor returns empty — later slices fall through to an honest
// shelf-less page, never a guess.
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QPointer>
#include <QUrl>
#include <QDateTime>

class QNetworkAccessManager;
class QNetworkReply;

class TankobanCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool remoteEnabled READ remoteEnabled NOTIFY revisionChanged)
    // No Q_PROPERTY(ready) here on purpose — see MalCatalog.h for the full note
    // (data-vault Slice 4, 2026-08-22 fix): it collided with the Q_INVOKABLE of the
    // same name and broke every `.ready()` call site in QML.
public:
    explicit TankobanCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~TankobanCatalog() override;

    Q_INVOKABLE bool ready() const { return m_ok; }
    // Data-vault Slice 2 (2026-08-22): reopen at a fresh path — see MalCatalog.
    Q_INVOKABLE bool reopen(const QString& dbPath);
    Q_INVOKABLE void closeForSwap();

    // {volumeCount:int, countBasis:string} — countBasis is "mal" or
    // "bookwalker". Empty map when the db is missing or malId is unknown.
    Q_INVOKABLE QVariantMap seriesInfo(int malId) const;

    // [{number:string, cover:string, name:string}], numeric-aware ordered
    // (2 before 10). Numbers "1".."N" are synthesized from seriesInfo's
    // volumeCount wherever no baked volume row exists for that number; baked
    // cover_url/name overlay the synthesized rows where present. Empty list
    // when the db is missing, malId is unknown, or the series has neither a
    // known count nor any baked volume rows.
    Q_INVOKABLE QVariantList volumes(int malId) const;

    // Optional semantic enrichment. No configured endpoint means no network.
    // ready() continues to describe the complete local SQLite catalogue only.
    void configureRemote(QNetworkAccessManager* nam, const QUrl& endpoint);
    Q_INVOKABLE void requestSeries(int malId, bool refresh = false);
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE bool hasSeries(int malId) const;
    int revision() const { return m_revision; }
    bool remoteEnabled() const { return !m_nam.isNull() && !m_endpoint.isEmpty(); }

signals:
    void readyChanged();
    void revisionChanged();
    void seriesChanged(int malId);
    void seriesFailed(int malId, QString reason);

private:
    bool openAt(const QString& dbPath);
    void fetchRemoteSeries(int malId, quint64 generation, bool renewLease);
    void issueLease(int malId, quint64 generation);
    void readReply(QNetworkReply* reply, int malId, quint64 generation,
                   bool issuingLease, bool renewLease);
    struct RemoteSeries { QVariantMap info; QVariantList volumes; QString sourceHash; QDateTime fetchedAt; };
    QHash<int, RemoteSeries> m_remoteSeries;
    QPointer<QNetworkAccessManager> m_nam;
    QPointer<QNetworkReply> m_reply;
    QUrl m_endpoint;
    QByteArray m_lease;
    quint64 m_generation = 0;
    int m_revision = 0;

    QSqlDatabase m_db;
    bool m_ok = false;
    QString m_conn;
};
