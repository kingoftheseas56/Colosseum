// TankobanCatalog.cpp — see header. [Agent 1 sub-exec (Sonnet), Tankoban catalogue independence]
#include "TankobanCatalog.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QSqlQuery>
#include <QVector>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDateTime>
#include <QSharedPointer>

#include <algorithm>

namespace {

// Zero-pad each run of digits so a plain lexicographic compare orders
// numerically ("2" before "10"); lowercases everything else for a stable,
// case-insensitive tiebreak on any non-numeric volume label. Mirrors
// VaultIndex::naturalSortKey's technique (native/engine/VaultIndex.cpp).
QString numericAwareKey(const QString& s)
{
    static const int kWidth = 12;
    QString out;
    out.reserve(s.size() + 16);
    int i = 0;
    const int n = s.size();
    while (i < n) {
        if (s.at(i).isDigit()) {
            int j = i;
            while (j < n && s.at(j).isDigit())
                ++j;
            const int len = j - i;
            if (len < kWidth)
                out += QString(kWidth - len, QLatin1Char('0'));
            out += s.mid(i, len);
            i = j;
        } else {
            out += s.at(i).toLower();
            ++i;
        }
    }
    return out;
}

} // namespace

bool TankobanCatalog::openAt(const QString& dbPath)
{
    // resolve beside the exe first (deployed), then the repo layout (dev run) — MalCatalog's ladder
    QString path = dbPath;
    if (!QFileInfo::exists(path)) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + dbPath;
        if (QFileInfo::exists(beside)) path = beside;
    }
    if (!QFileInfo::exists(path))
        return false;                             // no catalog — callers fall through honestly
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    return m_db.open();
}

TankobanCatalog::TankobanCatalog(const QString& dbPath, QObject* parent)
    : QObject(parent), m_conn(QStringLiteral("tankoban_catalog"))
{
    m_ok = openAt(dbPath);
}

TankobanCatalog::~TankobanCatalog()
{
    cancelRequest();
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
}

bool TankobanCatalog::reopen(const QString& dbPath)
{
    const bool wasOk = m_ok;
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
    m_ok = openAt(dbPath);
    if (m_ok != wasOk || m_ok)
        emit readyChanged();
    return m_ok;
}

void TankobanCatalog::closeForSwap()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
    const bool wasOk = m_ok;
    m_ok = false;
    if (wasOk)
        emit readyChanged();
}

QVariantMap TankobanCatalog::seriesInfo(int malId) const
{
    if (!m_ok)
        return m_remoteSeries.value(malId).info;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT volume_count, count_basis FROM series WHERE mal_id = ?"));
    q.addBindValue(malId);
    if (!q.exec() || !q.next())
        return m_remoteSeries.value(malId).info;
    QVariantMap m;
    m.insert(QStringLiteral("volumeCount"), q.value(0).toInt());
    m.insert(QStringLiteral("countBasis"), q.value(1).toString());
    return m;
}

QVariantList TankobanCatalog::volumes(int malId) const
{
    QVariantList out;
    if (!m_ok)
        return m_remoteSeries.value(malId).volumes;
    // A valid local series remains authoritative; remote facts fill only misses.
    QSqlQuery local(m_db);
    local.prepare(QStringLiteral("SELECT 1 FROM series WHERE mal_id = ?"));
    local.addBindValue(malId);
    if (local.exec() && !local.next() && m_remoteSeries.contains(malId))
        return m_remoteSeries.value(malId).volumes;

    const QVariantMap info = seriesInfo(malId);
    if (info.isEmpty())
        return out; // unknown malId — nothing to synthesize or overlay

    const int count = info.value(QStringLiteral("volumeCount")).toInt();

    // Baked cover/name facts keyed by their exact number string.
    QMap<QString, QPair<QString, QString>> overlay;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT number, cover_url, name FROM volumes WHERE mal_id = ?"));
        q.addBindValue(malId);
        if (q.exec()) {
            while (q.next())
                overlay.insert(q.value(0).toString(),
                               qMakePair(q.value(1).toString(), q.value(2).toString()));
        }
    }

    QVector<QVariantMap> rows;
    QSet<QString> emitted;

    // Synthesize "1".."N" from the known count, overlaying baked facts where present.
    for (int n = 1; n <= count; ++n) {
        const QString num = QString::number(n);
        const auto it = overlay.constFind(num);
        QVariantMap m;
        m.insert(QStringLiteral("number"), num);
        m.insert(QStringLiteral("cover"), it != overlay.constEnd() ? it.value().first : QString());
        m.insert(QStringLiteral("name"), it != overlay.constEnd() ? it.value().second : QString());
        rows.append(m);
        emitted.insert(num);
    }

    // Any baked rows outside the synthesized range (e.g. count unknown, or a
    // harvest number the count doesn't cover) still surface — nothing invented,
    // nothing dropped.
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it) {
        if (emitted.contains(it.key()))
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("number"), it.key());
        m.insert(QStringLiteral("cover"), it.value().first);
        m.insert(QStringLiteral("name"), it.value().second);
        rows.append(m);
    }

    std::sort(rows.begin(), rows.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return numericAwareKey(a.value(QStringLiteral("number")).toString())
             < numericAwareKey(b.value(QStringLiteral("number")).toString());
    });

    for (const auto& m : rows)
        out.append(m);
    return out;
}

void TankobanCatalog::configureRemote(QNetworkAccessManager* nam, const QUrl& endpoint)
{
    cancelRequest();
    m_lease.clear();
    m_nam = nam;
    m_endpoint = QUrl();
    // Production is HTTPS. Loopback HTTP is deliberately limited to test fixtures.
    if (endpoint.isValid() && !endpoint.host().isEmpty() && endpoint.userInfo().isEmpty() && endpoint.query().isEmpty()
        && endpoint.fragment().isEmpty() && endpoint.path().isEmpty()
        && (endpoint.scheme() == QStringLiteral("https")
            || (endpoint.scheme() == QStringLiteral("http")
                && (endpoint.host() == QStringLiteral("127.0.0.1")
                    || endpoint.host() == QStringLiteral("localhost")))))
        m_endpoint = endpoint;
    if (!m_remoteSeries.isEmpty()) {
        m_remoteSeries.clear();
        ++m_revision;
        emit revisionChanged();
    }
    else emit revisionChanged();
}

void TankobanCatalog::cancelRequest()
{
    ++m_generation;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

bool TankobanCatalog::hasSeries(int malId) const
{
    return !seriesInfo(malId).isEmpty() || !volumes(malId).isEmpty();
}

void TankobanCatalog::requestSeries(int malId, bool refresh)
{
    cancelRequest();
    if (malId <= 0) { emit seriesFailed(malId, QStringLiteral("INVALID_ID")); return; }
    bool local = false;
    if (m_ok) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("SELECT 1 FROM series WHERE mal_id = ? UNION SELECT 1 FROM volumes WHERE mal_id = ? LIMIT 1"));
        query.addBindValue(malId);
        query.addBindValue(malId);
        local = query.exec() && query.next();
    }
    const auto cached = m_remoteSeries.constFind(malId);
    if (local || (!refresh && cached != m_remoteSeries.constEnd()
        && cached->fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < 300)) {
        emit seriesChanged(malId);
        return;
    }
    if (!m_nam || m_endpoint.isEmpty()) {
        emit seriesFailed(malId, QStringLiteral("REMOTE_DISABLED"));
        return;
    }
    if (m_lease.isEmpty()) issueLease(malId, m_generation);
    else fetchRemoteSeries(malId, m_generation, true);
}

void TankobanCatalog::issueLease(int malId, quint64 generation)
{
    QNetworkRequest request(m_endpoint.resolved(QUrl(QStringLiteral("/v1/guest/session"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto* reply = m_nam->post(request, QByteArrayLiteral("{\"appId\":\"colosseum\",\"apiVersion\":1}"));
    readReply(reply, malId, generation, true, false);
}

void TankobanCatalog::fetchRemoteSeries(int malId, quint64 generation, bool renewLease)
{
    QNetworkRequest request(m_endpoint.resolved(QUrl(QStringLiteral("/v1/tankoban/series/%1").arg(malId))));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_lease);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    readReply(m_nam->get(request), malId, generation, false, renewLease);
}

void TankobanCatalog::readReply(QNetworkReply* reply, int malId, quint64 generation,
                               bool issuingLease, bool renewLease)
{
    m_reply = reply;
    const int cap = issuingLease ? 8192 : 512 * 1024;
    reply->setReadBufferSize(cap + 1);
    auto bytes = QSharedPointer<QByteArray>::create();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes, cap] {
        if (reply->isOpen()) bytes->append(reply->readAll());
        if (bytes->size() > cap) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, cap, malId, generation, issuingLease, renewLease] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply = nullptr;
        if (reply->isOpen()) bytes->append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!issuingLease && (status == 401 || status == 409) && renewLease) {
            m_lease.clear();
            issueLease(malId, generation);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status != 200 || bytes->size() > cap) {
            emit seriesFailed(malId, bytes->size() > cap ? QStringLiteral("RESPONSE_TOO_LARGE")
                : QStringLiteral("HTTP_%1").arg(status));
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(*bytes, &error);
        const auto root = doc.object();
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            emit seriesFailed(malId, QStringLiteral("INVALID_RESPONSE")); return;
        }
        if (issuingLease) {
            const auto token = root.value(QStringLiteral("token")).toString().toLatin1();
            const auto expiry = QDateTime::fromString(root.value(QStringLiteral("expiresAt")).toString(), Qt::ISODate);
            if (token.isEmpty() || token.size() > 2048 || !expiry.isValid()
                || expiry <= QDateTime::currentDateTimeUtc()) {
                emit seriesFailed(malId, QStringLiteral("INVALID_LEASE")); return;
            }
            m_lease = token;
            fetchRemoteSeries(malId, generation, false);
            return;
        }
        const auto info = root.value(QStringLiteral("seriesInfo"));
        const auto rows = root.value(QStringLiteral("volumes"));
        const auto hash = root.value(QStringLiteral("sourceHash")).toString();
        if (root.value(QStringLiteral("apiVersion")).toInt() != 1
            || root.value(QStringLiteral("ready")) != QJsonValue(true)
            || !QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(hash).hasMatch()
            || !info.isObject() || !rows.isArray() || rows.toArray().size() > 2000) {
            emit seriesFailed(malId, QStringLiteral("INCOMPATIBLE_RESPONSE")); return;
        }
        const auto object = info.toObject();
        const double count = object.value(QStringLiteral("volumeCount")).toDouble(-1);
        if (!object.isEmpty() && (count < 0 || count > 2000 || count != int(count)
            || !object.value(QStringLiteral("countBasis")).isString())) {
            emit seriesFailed(malId, QStringLiteral("INVALID_SERIES")); return;
        }
        QSet<QString> numbers;
        QVariantList volumes;
        for (const auto row : rows.toArray()) {
            const auto volume = row.toObject();
            const auto number = volume.value(QStringLiteral("number")).toString();
            if (!row.isObject() || number.isEmpty() || number.size() > 64 || numbers.contains(number)
                || !volume.value(QStringLiteral("cover")).isString()
                || !volume.value(QStringLiteral("name")).isString()) {
                emit seriesFailed(malId, QStringLiteral("INVALID_VOLUMES")); return;
            }
            numbers.insert(number);
            volumes.append(volume.toVariantMap());
        }
        if (object.isEmpty() && !volumes.isEmpty()) {
            emit seriesFailed(malId, QStringLiteral("INVALID_SERIES")); return;
        }
        // Bound this existing owner's transient enrichment, never a second durable store.
        if (!m_remoteSeries.isEmpty() && m_remoteSeries.constBegin()->sourceHash != hash)
            m_remoteSeries.clear();
        if (m_remoteSeries.size() >= 32) m_remoteSeries.erase(m_remoteSeries.begin());
        m_remoteSeries.insert(malId, {object.toVariantMap(), volumes, hash, QDateTime::currentDateTimeUtc()});
        ++m_revision;
        emit revisionChanged();
        emit seriesChanged(malId);
    });
}
