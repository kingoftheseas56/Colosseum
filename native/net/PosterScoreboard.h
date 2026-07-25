#pragma once
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Felt-speed arc, Stage 0 (spec 2026-07-24): the poster scoreboard.
// Every reply finishing on the QML image NAM lands in exactly ONE bucket per host:
//   Arrived       — HTTP success and (if webp) decodable on THIS machine
//   NetworkFailed — transport error or HTTP >= 400
//   Undecodable   — arrived as image/webp with no webp decoder present (the dev-hack scar)
// classify() is pure/static so the harness drives it without sockets. record() is
// mutex-guarded: the QML engine creates NAMs on more than one thread and replies
// finish on their own thread (watch() connects without a receiver context).
class PosterScoreboard : public QObject {
    Q_OBJECT
public:
    enum class Bucket { Arrived, NetworkFailed, Undecodable };

    explicit PosterScoreboard(QObject *parent = nullptr) : QObject(parent) {}

    void setWebpDecoderPresent(bool present) { m_webpPresent = present; }
    bool webpDecoderPresent() const { return m_webpPresent; }

    static Bucket classify(int httpStatus, const QString &contentType,
                           bool networkError, bool webpDecoderPresent);

    void record(const QString &host, int httpStatus, const QString &contentType,
                qint64 bytes, bool networkError);

    Q_INVOKABLE QVariantMap summary() const; // { host: {arrived, failed, undecodable, bytes} }
    QString summaryText() const;             // one log block; empty when nothing recorded

private:
    struct Row { qint64 arrived = 0; qint64 failed = 0; qint64 undecodable = 0; qint64 bytes = 0; };
    mutable QMutex m_mutex;
    QHash<QString, Row> m_rows;
    bool m_webpPresent = false;              // set once at boot, before any NAM exists
};
