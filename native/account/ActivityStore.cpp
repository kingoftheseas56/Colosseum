#include "ActivityStore.h"

#include "ActivityProjector.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

// Schema v1 exactly per CPP-PORT-CONTRACT.md §5. A DB stamped higher than
// this was created by a newer schema owner and is refused (fail closed),
// never silently downgraded — same discipline as VaultIndex::ensureSchema().
constexpr int kActivitySchemaVersion = 1;

QString generatedUuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

qint64 jsonInt(const QJsonObject &obj, const QString &key) {
    return static_cast<qint64>(obj.value(key).toDouble());
}

} // namespace

ActivityStore::ActivityStore(QObject *parent)
    : ActivityStore(QStringLiteral(":memory:"), parent) {}

ActivityStore::ActivityStore(const QString &databasePath, QObject *parent)
    : QObject(parent) {
    m_conn = QStringLiteral("activitystore_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(databasePath);

    if (databasePath != QLatin1String(":memory:")) {
        const QFileInfo info(databasePath);
        QDir().mkpath(info.absolutePath());
    }

    if (!m_db.open()) {
        m_openError = m_db.lastError().text();
        return;
    }

    // WAL is a no-op (silently ignored) for an in-memory database — harmless
    // either way. Recommended for crash resilience/reader-writer friction
    // (§5 "Database behavior").
    QSqlQuery walPragma(m_db);
    walPragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    walPragma.finish(); // release before ensureSchema() opens its own transaction

    if (!ensureSchema())
        m_db.close(); // ensureSchema() has already recorded m_openError
}

ActivityStore::~ActivityStore() {
    const QString conn = m_conn;
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase(); // drop our handle before removing the connection
    QSqlDatabase::removeDatabase(conn);
}

quint64 ActivityStore::revision() const {
    return m_revision;
}

bool ActivityStore::healthy(QString *error) const {
    if (m_db.isOpen())
        return true;
    if (error)
        *error = m_openError;
    return false;
}

bool ActivityStore::ensureSchema() {
    if (!m_db.isOpen())
        return false;

    // Every QSqlQuery below is explicitly .finish()ed as soon as its result is
    // consumed: the QSQLITE driver keeps a prepared statement handle alive
    // (SQL statements "in progress") until finish()/destruction, and SQLite
    // refuses to COMMIT a transaction while any such handle from the same
    // connection is still open — leaving one open silently turns every
    // ensureSchema() call into a failed commit.
    //
    // Each failure branch records m_openError from the QUERY's own lastError()
    // (not QSqlDatabase::lastError(), which does not reliably carry a per-
    // query QSQLITE error once that query object is gone) so healthy(&error)
    // reports a real diagnostic rather than an empty string.
    QSqlQuery versionQuery(m_db);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        m_openError = versionQuery.lastError().text();
        return false;
    }
    if (versionQuery.value(0).toInt() > kActivitySchemaVersion) {
        versionQuery.finish();
        m_openError = QStringLiteral("activity database schema is newer than this build supports");
        return false; // never downgrade a DB stamped by a newer schema owner
    }
    versionQuery.finish();

    if (!m_db.transaction()) {
        m_openError = m_db.lastError().text();
        return false;
    }

    QSqlQuery create(m_db);
    const bool tableOk = create.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS events ("
        " event_id TEXT PRIMARY KEY,"
        " schema_version INTEGER NOT NULL,"
        " type TEXT NOT NULL,"
        " session_id TEXT NOT NULL,"
        " world TEXT NOT NULL,"
        " kind TEXT NOT NULL,"
        " title_key TEXT NOT NULL,"
        " item_key TEXT NOT NULL,"
        " title TEXT NOT NULL,"
        " item_label TEXT NOT NULL,"
        " cover TEXT NOT NULL,"
        " utc_offset_minutes INTEGER NOT NULL,"
        " syncable INTEGER NOT NULL,"
        " source TEXT NOT NULL,"
        " start_at_ms INTEGER,"
        " end_at_ms INTEGER,"
        " active_ms INTEGER,"
        " rate_milli INTEGER,"
        " at_ms INTEGER,"
        " reading_form TEXT,"
        " page_keys_json TEXT,"
        " progress_micros INTEGER,"
        " completion_reason TEXT,"
        " canonical_json TEXT NOT NULL,"
        " canonical_hash BLOB NOT NULL)"));
    if (!tableOk) {
        m_openError = create.lastError().text();
        m_db.rollback();
        return false;
    }
    create.finish();

    // Recommended indexes, §5.
    static const QStringList indexStatements{
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_activity_type_start "
                       "ON events(type, start_at_ms)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_activity_type_at "
                       "ON events(type, at_ms)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_activity_session_kind_item_type "
                       "ON events(session_id, kind, item_key, type)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_activity_kind_item_type_at "
                       "ON events(kind, item_key, type, at_ms)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_activity_titlekey_type "
                       "ON events(title_key, type)"),
    };
    for (const QString &statement : indexStatements) {
        QSqlQuery index(m_db);
        if (!index.exec(statement)) {
            m_openError = index.lastError().text();
            m_db.rollback();
            return false;
        }
        index.finish();
    }

    if (!m_db.commit()) {
        m_openError = m_db.lastError().text();
        return false;
    }

    QSqlQuery stamp(m_db);
    if (!stamp.exec(QStringLiteral("PRAGMA user_version = %1").arg(kActivitySchemaVersion))) {
        m_openError = stamp.lastError().text();
        return false;
    }
    return true;
}

QString ActivityStore::newSessionId() const {
    return generatedUuid();
}

bool ActivityStore::insertFact(const QString &type, const QVariantMap &fact) {
    if (!healthy()) {
        emit integrityError(QStringLiteral("db_unhealthy"),
                            QStringLiteral("ActivityStore database is not open"));
        return false;
    }

    QJsonObject event = QJsonObject::fromVariantMap(fact);
    event.insert(QStringLiteral("v"), 1);
    event.insert(QStringLiteral("type"), type);

    QString eventId = event.value(QStringLiteral("eventId")).toString().trimmed();
    if (eventId.isEmpty()) {
        eventId = generatedUuid();
        event.insert(QStringLiteral("eventId"), eventId);
    }

    try {
        ActivityProjector::validateEvent(event);
    } catch (const ActivityProjector::ValidationError &error) {
        emit integrityError(QStringLiteral("invalid_event"), QString::fromUtf8(error.what()));
        return false;
    }

    const QString canonicalJson = ActivityProjector::canonicalEventJson(event);
    const QByteArray canonicalHash =
        QCryptographicHash::hash(canonicalJson.toUtf8(), QCryptographicHash::Sha256);

    QSqlQuery existing(m_db);
    existing.prepare(QStringLiteral("SELECT canonical_hash FROM events WHERE event_id = ?"));
    existing.addBindValue(eventId);
    if (!existing.exec()) {
        emit integrityError(QStringLiteral("db_error"), existing.lastError().text());
        return false;
    }
    if (existing.next()) {
        const QByteArray existingHash = existing.value(0).toByteArray();
        if (existingHash == canonicalHash)
            return true; // exact duplicate — idempotent success, no mutation, no revision bump

        // Conflicting payload for an existing eventId — reject, never
        // last-write-wins (§4 "Local insert idempotency").
        emit integrityError(QStringLiteral("event_conflict"),
                            QStringLiteral("eventId conflict: %1").arg(eventId));
        return false;
    }
    existing.finish(); // release the prepared SELECT before opening a write transaction

    if (!m_db.transaction()) {
        emit integrityError(QStringLiteral("db_error"), m_db.lastError().text());
        return false;
    }

    if (!insertEventRow(event, canonicalJson, canonicalHash)) {
        const QString error = m_db.lastError().text();
        m_db.rollback();
        emit integrityError(QStringLiteral("db_error"), error);
        return false;
    }

    if (!m_db.commit()) {
        const QString error = m_db.lastError().text();
        m_db.rollback();
        emit integrityError(QStringLiteral("db_error"), error);
        return false;
    }

    ++m_revision;
    emit changed();
    emit factCommitted(event.toVariantMap());
    return true;
}

QList<QVariantMap> ActivityStore::historyProjectionFacts() const {
    QList<QVariantMap> facts;
    if (!healthy())
        return facts;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT canonical_json FROM events")))
        return facts;
    while (query.next()) {
        const QJsonDocument document =
            QJsonDocument::fromJson(query.value(0).toString().toUtf8());
        if (!document.isObject())
            continue;
        const QJsonObject event = document.object();
        try {
            ActivityProjector::validateEvent(event);
            facts.append(event.toVariantMap());
        } catch (const ActivityProjector::ValidationError &) {
            continue;
        }
    }
    query.finish();
    std::sort(facts.begin(), facts.end(), [](const QVariantMap &left, const QVariantMap &right) {
        const QString leftType = left.value(QStringLiteral("type")).toString();
        const QString rightType = right.value(QStringLiteral("type")).toString();
        const qint64 leftAt = leftType == QLatin1String("playback_delta")
            ? left.value(QStringLiteral("startAtMs")).toLongLong()
            : left.value(QStringLiteral("atMs")).toLongLong();
        const qint64 rightAt = rightType == QLatin1String("playback_delta")
            ? right.value(QStringLiteral("startAtMs")).toLongLong()
            : right.value(QStringLiteral("atMs")).toLongLong();
        if (leftAt != rightAt)
            return leftAt < rightAt;
        return left.value(QStringLiteral("eventId")).toString()
            < right.value(QStringLiteral("eventId")).toString();
    });
    return facts;
}

bool ActivityStore::insertEventRow(const QJsonObject &event, const QString &canonicalJson,
                                    const QByteArray &canonicalHash) {
    const QString type = event.value(QStringLiteral("type")).toString();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO events ("
        " event_id, schema_version, type, session_id, world, kind, title_key, item_key,"
        " title, item_label, cover, utc_offset_minutes, syncable, source,"
        " start_at_ms, end_at_ms, active_ms, rate_milli,"
        " at_ms, reading_form, page_keys_json, progress_micros, completion_reason,"
        " canonical_json, canonical_hash"
        ") VALUES ("
        " :event_id, :schema_version, :type, :session_id, :world, :kind, :title_key, :item_key,"
        " :title, :item_label, :cover, :utc_offset_minutes, :syncable, :source,"
        " :start_at_ms, :end_at_ms, :active_ms, :rate_milli,"
        " :at_ms, :reading_form, :page_keys_json, :progress_micros, :completion_reason,"
        " :canonical_json, :canonical_hash)"));

    query.bindValue(QStringLiteral(":event_id"), event.value(QStringLiteral("eventId")).toString());
    query.bindValue(QStringLiteral(":schema_version"), 1);
    query.bindValue(QStringLiteral(":type"), type);
    query.bindValue(QStringLiteral(":session_id"), event.value(QStringLiteral("sessionId")).toString());
    query.bindValue(QStringLiteral(":world"), event.value(QStringLiteral("world")).toString());
    query.bindValue(QStringLiteral(":kind"), event.value(QStringLiteral("kind")).toString());
    query.bindValue(QStringLiteral(":title_key"), event.value(QStringLiteral("titleKey")).toString());
    query.bindValue(QStringLiteral(":item_key"), event.value(QStringLiteral("itemKey")).toString());
    query.bindValue(QStringLiteral(":title"), event.value(QStringLiteral("title")).toString());
    query.bindValue(QStringLiteral(":item_label"), event.value(QStringLiteral("itemLabel")).toString());
    query.bindValue(QStringLiteral(":cover"), event.value(QStringLiteral("cover")).toString());
    query.bindValue(QStringLiteral(":utc_offset_minutes"), jsonInt(event, QStringLiteral("utcOffsetMinutes")));
    query.bindValue(QStringLiteral(":syncable"), event.value(QStringLiteral("syncable")).toBool());
    query.bindValue(QStringLiteral(":source"), event.value(QStringLiteral("source")).toString());

    // Type-specific columns: NULL (QVariant()) wherever not applicable to
    // this event's type — SQLite is dynamically typed per-column, so an
    // untyped NULL binding is unambiguous regardless of column affinity.
    if (type == QLatin1String("playback_delta")) {
        query.bindValue(QStringLiteral(":start_at_ms"), jsonInt(event, QStringLiteral("startAtMs")));
        query.bindValue(QStringLiteral(":end_at_ms"), jsonInt(event, QStringLiteral("endAtMs")));
        query.bindValue(QStringLiteral(":active_ms"), jsonInt(event, QStringLiteral("activeMs")));
        query.bindValue(QStringLiteral(":rate_milli"), jsonInt(event, QStringLiteral("rateMilli")));
        query.bindValue(QStringLiteral(":at_ms"), QVariant());
        query.bindValue(QStringLiteral(":reading_form"), QVariant());
        query.bindValue(QStringLiteral(":page_keys_json"), QVariant());
        query.bindValue(QStringLiteral(":progress_micros"), QVariant());
        query.bindValue(QStringLiteral(":completion_reason"), QVariant());
    } else {
        query.bindValue(QStringLiteral(":start_at_ms"), QVariant());
        query.bindValue(QStringLiteral(":end_at_ms"), QVariant());
        query.bindValue(QStringLiteral(":active_ms"), QVariant());
        query.bindValue(QStringLiteral(":rate_milli"), QVariant());
        query.bindValue(QStringLiteral(":at_ms"), jsonInt(event, QStringLiteral("atMs")));

        if (type == QLatin1String("reading_delta")) {
            query.bindValue(QStringLiteral(":reading_form"), event.value(QStringLiteral("readingForm")).toString());
            const QJsonArray pageKeys = event.value(QStringLiteral("pageKeys")).toArray();
            query.bindValue(QStringLiteral(":page_keys_json"),
                            QString::fromUtf8(QJsonDocument(pageKeys).toJson(QJsonDocument::Compact)));
            query.bindValue(QStringLiteral(":progress_micros"), jsonInt(event, QStringLiteral("progressMicros")));
            query.bindValue(QStringLiteral(":completion_reason"), QVariant());
        } else { // media_completed
            query.bindValue(QStringLiteral(":reading_form"), QVariant());
            query.bindValue(QStringLiteral(":page_keys_json"), QVariant());
            query.bindValue(QStringLiteral(":progress_micros"), QVariant());
            query.bindValue(QStringLiteral(":completion_reason"), event.value(QStringLiteral("reason")).toString());
        }
    }

    query.bindValue(QStringLiteral(":canonical_json"), canonicalJson);
    query.bindValue(QStringLiteral(":canonical_hash"), canonicalHash);

    return query.exec();
}

bool ActivityStore::recordPlaybackDelta(const QVariantMap &fact) {
    return insertFact(QStringLiteral("playback_delta"), fact);
}

bool ActivityStore::recordReadingDelta(const QVariantMap &fact) {
    return insertFact(QStringLiteral("reading_delta"), fact);
}

bool ActivityStore::recordCompletion(const QVariantMap &fact) {
    return insertFact(QStringLiteral("media_completed"), fact);
}

QVariantMap ActivityStore::projectMonth(const QString &monthKey) const {
    if (!healthy()) {
        const_cast<ActivityStore *>(this)->integrityError(
            QStringLiteral("db_unhealthy"), QStringLiteral("ActivityStore database is not open"));
        return QVariantMap();
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT canonical_json FROM events"))) {
        const_cast<ActivityStore *>(this)->integrityError(
            QStringLiteral("db_error"), query.lastError().text());
        return QVariantMap();
    }

    QJsonArray ledger;
    while (query.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toString().toUtf8());
        if (doc.isObject())
            ledger.append(doc.object());
    }

    try {
        return ActivityProjector::projectMonth(ledger, monthKey).toVariantMap();
    } catch (const ActivityProjector::ValidationError &error) {
        const_cast<ActivityStore *>(this)->integrityError(
            QStringLiteral("invalid_month_key"), QString::fromUtf8(error.what()));
        return QVariantMap();
    }
}

QString ActivityStore::earliestActivityMonth() const {
    if (!healthy())
        return QString();

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT type, start_at_ms, at_ms, utc_offset_minutes FROM events")))
        return QString();

    QString earliest;
    while (query.next()) {
        const QString type = query.value(0).toString();
        const qint64 ms = type == QLatin1String("playback_delta")
            ? query.value(1).toLongLong()
            : query.value(2).toLongLong();
        const qint64 offsetMinutes = query.value(3).toLongLong();
        const QString monthKey = ActivityProjector::localMonthKey(ms, offsetMinutes);
        if (earliest.isEmpty() || monthKey < earliest)
            earliest = monthKey;
    }
    return earliest;
}

bool ActivityStore::hasFixedCoverage(const QString &kind, const QString &itemKey,
                                      const QVariantList &requiredPageKeys) const {
    if (!healthy())
        return false;
    if (requiredPageKeys.isEmpty())
        return true; // vacuously covered

    QSet<QString> required;
    for (const QVariant &value : requiredPageKeys)
        required.insert(value.toString());

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT page_keys_json FROM events "
        "WHERE type = 'reading_delta' AND reading_form = 'fixed' AND kind = ? AND item_key = ?"));
    query.addBindValue(kind);
    query.addBindValue(itemKey);
    if (!query.exec())
        return false;

    QSet<QString> covered;
    while (query.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toString().toUtf8());
        for (const QJsonValue &pageKey : doc.array())
            covered.insert(pageKey.toString());
    }

    for (const QString &key : std::as_const(required)) {
        if (!covered.contains(key))
            return false;
    }
    return true;
}

bool ActivityStore::checkpointForSafeCopy(QString *error) {
    if (!healthy(error))
        return false;

    QSqlQuery checkpoint(m_db);
    if (!checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))) {
        if (error)
            *error = checkpoint.lastError().text();
        return false;
    }
    checkpoint.finish();
    return true;
}

QString ActivityStore::fileDigestSha256(const QString &path) {
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QString();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();

    return QString::fromLatin1(hash.result().toHex());
}

bool ActivityStore::clearAll() {
    if (!healthy()) {
        emit integrityError(QStringLiteral("db_unhealthy"),
                            QStringLiteral("ActivityStore database is not open"));
        return false;
    }

    if (!m_db.transaction()) {
        emit integrityError(QStringLiteral("db_error"), m_db.lastError().text());
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM events"))) {
        const QString error = query.lastError().text();
        m_db.rollback();
        emit integrityError(QStringLiteral("db_error"), error);
        return false;
    }
    query.finish(); // release the statement before committing (see ensureSchema() note)

    if (!m_db.commit()) {
        const QString error = m_db.lastError().text();
        m_db.rollback();
        emit integrityError(QStringLiteral("db_error"), error);
        return false;
    }

    ++m_revision;
    emit changed();
    return true;
}
