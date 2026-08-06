#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class QNetworkReply;
class QUrl;

// BiblioImageDiag — the per-REQUEST image-network recorder behind the Lanista
// `biblio.imageDiag` probe (decision brief 2026-08-06, §4). PosterScoreboard
// answers "how healthy is each HOST overall"; this answers the question that
// actually diagnoses one blank card: "what happened to THIS URL" — status,
// error, cache hit, bytes, timing. Named for the Biblio cover pilot that
// motivated it, but it records every request through the shared caching NAM
// (all worlds' posters ride the same factory); the scope is the seam, not the
// mode.
//
// Threading mirrors PosterScoreboard exactly: record() runs on the reply's own
// thread (the QML engine creates NAMs on multiple threads), so the ring is
// mutex-guarded and the object has no thread affinity requirements beyond
// living on the GUI thread for invoke-read's Qt::DirectConnection reads.
//
// Bounded by design: a fixed ring (newest wins) — this is a diagnostic window,
// not a history store. It never touches the reply body and never delays it.
class BiblioImageDiag : public QObject
{
    Q_OBJECT
public:
    explicit BiblioImageDiag(QObject *parent = nullptr) : QObject(parent) {}

    // Wire one reply into the recorder. Called from createRequest on whatever
    // thread the NAM lives on; `requestedUrl` is the PRE-pin-rewrite URL — the
    // one QML asked for and the one a card's `source` property will match.
    void track(QNetworkReply *reply, const QUrl &requestedUrl);

    // ── Lanista invoke-read surface (QString args, QVariantList returns — the
    //    bridge's marshaling contract). Newest rows first in both. ──

    // Rows whose requested URL CONTAINS the fragment (exact URLs work verbatim;
    // a card id or filename fragment works too). Empty fragment matches nothing
    // — that would be recentRows' job, and an accidental "" must not dump the ring.
    Q_INVOKABLE QVariantList rowsForUrl(const QString &urlFragment) const;

    // The newest N rows. The bridge marshals every argument as QString, so the
    // count arrives as text; unparseable or <=0 falls back to 25.
    Q_INVOKABLE QVariantList recentRows(const QString &limitText) const;

private:
    struct Row {
        QString url;          // as requested (pre-rewrite)
        QString finalUrl;     // after redirects, if any
        qint64 startedNs = 0; // monotonic, from track() time
        qint64 finishedNs = 0;
        int httpStatus = 0;
        int networkError = 0; // QNetworkReply::NetworkError value; 0 = NoError
        QString errorString;  // empty when no error
        QString contentType;
        qint64 bytes = 0;     // Content-Length when present, else bytesAvailable
        bool cacheHit = false;
    };

    void push(Row row);
    static QVariantMap rowToMap(const Row &r);

    static constexpr int kCapacity = 512;
    mutable QMutex m_mutex;
    QVector<Row> m_rows;  // ring: newest appended, oldest dropped at capacity
};
