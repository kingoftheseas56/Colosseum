#include "RatingsReviewsStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace {

constexpr int kSchemaVersion = 1;
constexpr int kReviewMaxUtf8Bytes = 16384;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool hasExactKeys(const QJsonObject &object,
                  std::initializer_list<QString> expected)
{
    QSet<QString> actual;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        actual.insert(it.key());

    QSet<QString> wanted;
    for (const QString &key : expected)
        wanted.insert(key);
    return actual == wanted;
}

bool validRecordKey(const QString &key)
{
    if (!key.startsWith(QStringLiteral("rr1:")) || key.size() != 68)
        return false;
    for (qsizetype i = 4; i < key.size(); ++i) {
        const QChar ch = key.at(i);
        if (!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
              || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))))
            return false;
    }
    return true;
}

bool validateIdentity(const RatingsReviewsStore::Identity &identity,
                      QString *error)
{
    if (identity.world.isEmpty()
        || identity.world != identity.world.trimmed()
        || identity.world != identity.world.toLower()) {
        setError(error, QStringLiteral(
            "Ratings/reviews world must be non-empty, trimmed, and lower-case."));
        return false;
    }
    if (identity.kind.isEmpty()
        || identity.kind != identity.kind.trimmed()
        || identity.kind != identity.kind.toLower()) {
        setError(error, QStringLiteral(
            "Ratings/reviews kind must be non-empty, trimmed, and lower-case."));
        return false;
    }
    if (identity.mediaId.isEmpty()) {
        setError(error, QStringLiteral(
            "Ratings/reviews mediaId must be non-empty."));
        return false;
    }
    return true;
}

void appendLengthPrefixed(QByteArray *target, const QByteArray &value)
{
    const quint32 length = static_cast<quint32>(value.size());
    target->append(static_cast<char>((length >> 24) & 0xff));
    target->append(static_cast<char>((length >> 16) & 0xff));
    target->append(static_cast<char>((length >> 8) & 0xff));
    target->append(static_cast<char>(length & 0xff));
    target->append(value);
}

bool validRating(const std::optional<double> &rating)
{
    if (!rating)
        return true;
    const double value = *rating;
    if (!std::isfinite(value) || value < 0.0 || value > 10.0)
        return false;
    const double doubled = value * 2.0;
    return std::floor(doubled) == doubled;
}

bool validReview(const std::optional<QString> &review)
{
    return !review || review->toUtf8().size() <= kReviewMaxUtf8Bytes;
}

bool recordsEqual(const RatingsReviewsStore::Record &left,
                  const RatingsReviewsStore::Record &right)
{
    return left.identity.world == right.identity.world
        && left.identity.kind == right.identity.kind
        && left.identity.mediaId == right.identity.mediaId
        && left.rating == right.rating
        && left.review == right.review
        && left.spoiler == right.spoiler
        && left.createdAtMs == right.createdAtMs
        && left.updatedAtMs == right.updatedAtMs;
}

QJsonObject recordToJson(const RatingsReviewsStore::Record &record)
{
    QJsonObject object;
    object.insert(QStringLiteral("world"), record.identity.world);
    object.insert(QStringLiteral("kind"), record.identity.kind);
    object.insert(QStringLiteral("media_id"), record.identity.mediaId);
    object.insert(QStringLiteral("rating"),
                  record.rating ? QJsonValue(*record.rating) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("review"),
                  record.review ? QJsonValue(*record.review) : QJsonValue(QJsonValue::Null));

    object.insert(QStringLiteral("spoiler"), record.spoiler);
    object.insert(QStringLiteral("created_at_ms"),
                  static_cast<double>(record.createdAtMs));
    object.insert(QStringLiteral("updated_at_ms"),
                  static_cast<double>(record.updatedAtMs));
    return object;
}

QByteArray mergeTieDigest(const QJsonObject &record)
{
    QJsonArray array;
    array.append(record.value(QStringLiteral("world")));
    array.append(record.value(QStringLiteral("kind")));
    array.append(record.value(QStringLiteral("media_id")));
    array.append(record.value(QStringLiteral("rating")));
    array.append(record.value(QStringLiteral("review")));
    array.append(record.value(QStringLiteral("spoiler")));
    array.append(record.value(QStringLiteral("created_at_ms")));
    array.append(record.value(QStringLiteral("updated_at_ms")));
    return QCryptographicHash::hash(
        QJsonDocument(array).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
}

bool parseInteger(const QJsonValue &value, qint64 minimum, qint64 *out)
{
    if (!value.isDouble() || !out)
        return false;
    const double raw = value.toDouble();
    if (!std::isfinite(raw)
        || std::floor(raw) != raw
        || raw < static_cast<double>(minimum)
        || raw > 9007199254740991.0) {
        return false;
    }
    *out = static_cast<qint64>(raw);
    return true;
}

bool parseRevision(const QJsonValue &value, quint64 *out)
{
    qint64 parsed = 0;
    if (!parseInteger(value, 0, &parsed))
        return false;
    *out = static_cast<quint64>(parsed);
    return true;
}

bool parseRecord(const QString &recordKey,
                 const QJsonValue &value,
                 RatingsReviewsStore::Record *out,
                 QString *error)
{

    if (!validRecordKey(recordKey) || !value.isObject()) {
        setError(error, QStringLiteral("Ratings/reviews record key or value is malformed."));
        return false;
    }

    const QJsonObject object = value.toObject();
    if (!hasExactKeys(object, {
            QStringLiteral("world"),
            QStringLiteral("kind"),
            QStringLiteral("media_id"),
            QStringLiteral("rating"),
            QStringLiteral("review"),
            QStringLiteral("spoiler"),
            QStringLiteral("created_at_ms"),
            QStringLiteral("updated_at_ms")})) {
        setError(error, QStringLiteral("Ratings/reviews record has an invalid field set."));
        return false;
    }

    if (!object.value(QStringLiteral("world")).isString()
        || !object.value(QStringLiteral("kind")).isString()
        || !object.value(QStringLiteral("media_id")).isString()
        || !object.value(QStringLiteral("spoiler")).isBool()) {
        setError(error, QStringLiteral("Ratings/reviews record has invalid field types."));
        return false;
    }

    RatingsReviewsStore::Record record;
    record.identity.world = object.value(QStringLiteral("world")).toString();
    record.identity.kind = object.value(QStringLiteral("kind")).toString();
    record.identity.mediaId = object.value(QStringLiteral("media_id")).toString();
    record.spoiler = object.value(QStringLiteral("spoiler")).toBool();

    const QJsonValue ratingValue = object.value(QStringLiteral("rating"));
    if (ratingValue.isNull()) {
        record.rating.reset();
    } else if (ratingValue.isDouble()) {
        record.rating = ratingValue.toDouble();
    } else {
        setError(error, QStringLiteral("Ratings/reviews rating must be null or numeric."));
        return false;
    }

    const QJsonValue reviewValue = object.value(QStringLiteral("review"));
    if (reviewValue.isNull()) {
        record.review.reset();
    } else if (reviewValue.isString()) {
        record.review = reviewValue.toString();
    } else {
        setError(error, QStringLiteral("Ratings/reviews review must be null or text."));
        return false;
    }

    if (!parseInteger(object.value(QStringLiteral("created_at_ms")), 1,
                      &record.createdAtMs)
        || !parseInteger(object.value(QStringLiteral("updated_at_ms")), 1,
                         &record.updatedAtMs)) {
        setError(error, QStringLiteral("Ratings/reviews timestamps must be positive integers."));
        return false;
    }

    QString identityError;
    const QString expectedKey =
        RatingsReviewsStore::recordKeyForIdentity(record.identity, &identityError);
    if (expectedKey.isEmpty() || expectedKey != recordKey) {
        setError(error, identityError.isEmpty()
            ? QStringLiteral("Ratings/reviews record key does not match its identity.")
            : identityError);
        return false;
    }
    if (!validRating(record.rating) || !validReview(record.review)) {
        setError(error, QStringLiteral("Ratings/reviews rating or review is outside its canonical domain."));
        return false;
    }
    if (!record.review && record.spoiler) {
        setError(error, QStringLiteral("A null canonical review cannot be marked as a spoiler."));
        return false;
    }

    if (!record.rating && !record.review) {
        setError(error, QStringLiteral(
            "A live ratings/reviews record must contain a rating or review."));
        return false;
    }
    if (record.updatedAtMs < record.createdAtMs) {
        setError(error, QStringLiteral(
            "Ratings/reviews updated_at_ms cannot precede created_at_ms."));
        return false;
    }

    if (out)
        *out = record;
    return true;
}

bool parseTombstone(const QString &recordKey,
                    const QJsonValue &value,
                    qint64 *deletedAtMs,
                    QString *error)
{
    if (!validRecordKey(recordKey) || !value.isObject()) {
        setError(error, QStringLiteral("Ratings/reviews tombstone key or value is malformed."));
        return false;
    }

    const QJsonObject object = value.toObject();
    if (!hasExactKeys(object, {QStringLiteral("deleted_at_ms")})
        || !parseInteger(object.value(QStringLiteral("deleted_at_ms")), 1,
                         deletedAtMs)) {
        setError(error, QStringLiteral(
            "Ratings/reviews tombstone must contain one positive deleted_at_ms."));
        return false;
    }
    return true;
}

bool validateCanonicalMaps(const QJsonObject &records,
                           const QJsonObject &tombstones,
                           QString *error)
{
    for (auto it = records.constBegin(); it != records.constEnd(); ++it) {
        if (tombstones.contains(it.key())) {
            setError(error, QStringLiteral(
                "Ratings/reviews live records and tombstones must be disjoint."));
            return false;
        }
        RatingsReviewsStore::Record parsed;
        if (!parseRecord(it.key(), it.value(), &parsed, error))
            return false;
    }

    for (auto it = tombstones.constBegin(); it != tombstones.constEnd(); ++it) {
        qint64 deletedAtMs = 0;
        if (!parseTombstone(it.key(), it.value(), &deletedAtMs, error))
            return false;
    }
    return true;
}

qint64 eventTimeForKey(const QString &key,
                       const QJsonObject &records,
                       const QJsonObject &tombstones)
{
    if (records.contains(key)) {
        return records.value(key).toObject()
            .value(QStringLiteral("updated_at_ms")).toInteger();
    }
    if (tombstones.contains(key)) {
        return tombstones.value(key).toObject()
            .value(QStringLiteral("deleted_at_ms")).toInteger();
    }
    return 0;
}

QJsonObject tombstoneToJson(qint64 deletedAtMs)
{
    return {{QStringLiteral("deleted_at_ms"),
             static_cast<double>(deletedAtMs)}};
}

QJsonObject storeToJson(quint64 revision,
                        const QJsonObject &records,
                        const QJsonObject &tombstones)
{
    return {
        {QStringLiteral("version"), kSchemaVersion},
        {QStringLiteral("revision"), static_cast<double>(revision)},
        {QStringLiteral("records"), records},
        {QStringLiteral("tombstones"), tombstones},
    };
}

} // namespace

RatingsReviewsStore::RatingsReviewsStore(
    const QString &path,
    Clock clock,
    QObject *parent)
    : QObject(parent),
      m_path(path),
      m_clock(clock ? std::move(clock) : Clock([] {
          return QDateTime::currentMSecsSinceEpoch();
      }))
{
    setObjectName(QStringLiteral("ratingsReviewsStore"));
    load();
}

bool RatingsReviewsStore::healthy(QString *error) const
{
    if (!m_healthy)
        setError(error, m_persistenceError);
    else if (error)
        error->clear();
    return m_healthy;
}

QString RatingsReviewsStore::persistenceError() const
{
    return m_persistenceError;
}

QString RatingsReviewsStore::storagePath() const
{
    return m_path;
}

quint64 RatingsReviewsStore::revision() const
{
    return m_revision;
}

QString RatingsReviewsStore::recordKeyForIdentity(
    const Identity &identity,
    QString *error)
{

    if (error)
        error->clear();
    if (!validateIdentity(identity, error))
        return {};

    const QByteArray world = identity.world.toUtf8();
    const QByteArray kind = identity.kind.toUtf8();
    const QByteArray mediaId = identity.mediaId.toUtf8();

    QByteArray preimage;
    preimage.reserve(world.size() + kind.size() + mediaId.size() + 12);
    appendLengthPrefixed(&preimage, world);
    appendLengthPrefixed(&preimage, kind);
    appendLengthPrefixed(&preimage, mediaId);

    return QStringLiteral("rr1:")
        + QString::fromLatin1(
            QCryptographicHash::hash(preimage, QCryptographicHash::Sha256)
                .toHex());
}

std::optional<RatingsReviewsStore::Record>
RatingsReviewsStore::record(const Identity &identity) const
{
    const QString key = recordKeyForIdentity(identity);
    return key.isEmpty() ? std::nullopt : recordByKey(key);
}

std::optional<RatingsReviewsStore::Record>
RatingsReviewsStore::recordByKey(const QString &recordKey) const
{
    const auto it = m_records.constFind(recordKey);
    if (it == m_records.constEnd())
        return std::nullopt;

    Record parsed;
    QString ignored;
    if (!parseRecord(recordKey, it.value(), &parsed, &ignored))
        return std::nullopt;
    return parsed;
}

QJsonObject RatingsReviewsStore::recordsJson() const
{
    return m_records;
}

QJsonObject RatingsReviewsStore::tombstonesJson() const
{
    return m_tombstones;
}

bool RatingsReviewsStore::requireHealthy(QString *error) const
{
    if (m_healthy)
        return true;
    setError(error, m_persistenceError);
    return false;
}

bool RatingsReviewsStore::load()
{
    m_records = {};
    m_tombstones = {};
    m_revision = 0;
    m_healthy = true;
    m_persistenceError.clear();

    if (m_path.isEmpty()) {
        m_healthy = false;
        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence path is empty.");
        return false;
    }

    const QFileInfo info(m_path);
    if (!info.exists())
        return true;
    if (!info.isFile()) {
        m_healthy = false;
        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence path is not a file.");
        return false;
    }

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;

        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence file could not be opened.");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        m_healthy = false;
        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence file is malformed JSON.");
        return false;
    }

    const QJsonObject root = document.object();
    qint64 schemaVersion = 0;
    if (!hasExactKeys(root, {
            QStringLiteral("version"),
            QStringLiteral("revision"),
            QStringLiteral("records"),
            QStringLiteral("tombstones")})
        || !parseInteger(root.value(QStringLiteral("version")), 1, &schemaVersion)
        || schemaVersion != kSchemaVersion
        || !root.value(QStringLiteral("records")).isObject()
        || !root.value(QStringLiteral("tombstones")).isObject()) {
        m_healthy = false;

        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence schema is invalid.");
        return false;
    }

    quint64 revisionValue = 0;
    const QJsonObject records =
        root.value(QStringLiteral("records")).toObject();
    const QJsonObject tombstones =
        root.value(QStringLiteral("tombstones")).toObject();
    QString validationError;
    if (!parseRevision(root.value(QStringLiteral("revision")), &revisionValue)
        || !validateCanonicalMaps(records, tombstones, &validationError)) {
        m_healthy = false;
        m_persistenceError = validationError.isEmpty()
            ? QStringLiteral("Ratings/reviews revision is invalid.")
            : validationError;
        return false;
    }

    m_records = records;
    m_tombstones = tombstones;
    m_revision = revisionValue;
    return true;
}

bool RatingsReviewsStore::persistCandidate(
    const QJsonObject &records,
    const QJsonObject &tombstones,
    quint64 revisionValue,
    QString *error)
{
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.absolutePath())) {
        m_healthy = false;
        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence directory could not be created.");
        setError(error, m_persistenceError);
        return false;
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_healthy = false;
        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence file could not be opened for writing.");
        setError(error, m_persistenceError);
        return false;
    }

    const QByteArray bytes =
        QJsonDocument(storeToJson(revisionValue, records, tombstones))
            .toJson(QJsonDocument::Compact);

    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_healthy = false;
        m_persistenceError =
            QStringLiteral("Ratings/reviews persistence commit failed.");
        setError(error, m_persistenceError);
        return false;
    }

    return true;
}

bool RatingsReviewsStore::commitCandidate(
    const QJsonObject &records,
    const QJsonObject &tombstones,
    const QString &recordKey,
    MutationOrigin origin,
    CommitResult *result,
    QString *error)
{
    if (!requireHealthy(error))
        return false;
    if (m_revision >= 9007199254740991ULL) {
        setError(error, QStringLiteral("Ratings/reviews revision limit was reached."));
        return false;
    }

    const quint64 nextRevision = m_revision + 1;
    if (!persistCandidate(records, tombstones, nextRevision, error))
        return false;

    m_records = records;
    m_tombstones = tombstones;
    m_revision = nextRevision;

    if (result) {
        result->changed = true;
        result->revision = m_revision;
        result->recordKey = recordKey;
    }

    emit changed();
    if (origin == MutationOrigin::Local) {
        emit syncDirty(m_revision);
    } else if (origin == MutationOrigin::Remote) {
        emit remoteApplied(recordKey, m_revision);
    }

    return true;
}

qint64 RatingsReviewsStore::now(QString *error) const
{
    const qint64 value = m_clock ? m_clock() : 0;
    if (value <= 0) {
        setError(error, QStringLiteral(
            "Ratings/reviews mutation clock must return a positive epoch millisecond."));
        return 0;
    }
    return value;
}

bool RatingsReviewsStore::saveLocal(
    const Identity &identity,
    const std::optional<double> &rating,
    const std::optional<QString> &review,
    bool spoiler,
    CommitResult *result,
    QString *error)
{
    if (error)
        error->clear();
    if (!requireHealthy(error))
        return false;

    const QString key = recordKeyForIdentity(identity, error);
    if (key.isEmpty())
        return false;
    if (!validRating(rating)) {
        setError(error, QStringLiteral(
            "Canonical rating must be null or a half-point from 0 through 10."));
        return false;
    }
    if (!validReview(review)) {
        setError(error, QStringLiteral(
            "Canonical review exceeds the 16,384 UTF-8 byte limit."));
        return false;
    }
    if (!review && spoiler) {
        setError(error, QStringLiteral(
            "A null canonical review cannot be marked as a spoiler."));
        return false;
    }

    const std::optional<Record> current = recordByKey(key);
    if (rating || review) {
        if (current
            && current->rating == rating
            && current->review == review
            && current->spoiler == spoiler) {
            if (result) {
                result->changed = false;
                result->revision = m_revision;
                result->recordKey = key;
            }
            return true;
        }
    } else if (!current && m_tombstones.contains(key)) {
        if (result) {
            result->changed = false;
            result->revision = m_revision;
            result->recordKey = key;
        }
        return true;
    }

    qint64 eventMs = now(error);
    if (eventMs <= 0)
        return false;

    const qint64 previousEvent =
        eventTimeForKey(key, m_records, m_tombstones);
    if (eventMs <= previousEvent) {
        if (previousEvent == std::numeric_limits<qint64>::max()) {
            setError(error, QStringLiteral(
                "Ratings/reviews event timestamp limit was reached."));
            return false;
        }
        eventMs = previousEvent + 1;
    }

    QJsonObject candidateRecords = m_records;
    QJsonObject candidateTombstones = m_tombstones;
    if (!rating && !review) {
        candidateRecords.remove(key);
        candidateTombstones.insert(key, tombstoneToJson(eventMs));
        return commitCandidate(
            candidateRecords, candidateTombstones, key,
            MutationOrigin::Local, result, error);
    }

    Record candidate;
    candidate.identity = identity;
    candidate.rating = rating;
    candidate.review = review;

    candidate.spoiler = spoiler;
    candidate.createdAtMs = current ? current->createdAtMs : eventMs;
    candidate.updatedAtMs = eventMs;

    candidateRecords.insert(key, recordToJson(candidate));
    candidateTombstones.remove(key);
    return commitCandidate(
        candidateRecords, candidateTombstones, key,
        MutationOrigin::Local, result, error);
}

bool RatingsReviewsStore::setRating(
    const Identity &identity,
    double rating,
    CommitResult *result,
    QString *error)
{
    const auto current = record(identity);
    return saveLocal(
        identity,
        rating,
        current ? current->review : std::optional<QString>{},
        current ? current->spoiler : false,
        result,
        error);
}

bool RatingsReviewsStore::clearRating(
    const Identity &identity,
    CommitResult *result,
    QString *error)
{
    const auto current = record(identity);
    return saveLocal(
        identity,
        std::nullopt,
        current ? current->review : std::optional<QString>{},
        current ? current->spoiler : false,
        result,
        error);
}

bool RatingsReviewsStore::setReview(
    const Identity &identity,
    const QString &review,
    bool spoiler,
    CommitResult *result,
    QString *error)
{
    const auto current = record(identity);
    return saveLocal(
        identity,
        current ? current->rating : std::optional<double>{},
        review,
        spoiler,
        result,
        error);
}

bool RatingsReviewsStore::deleteReview(
    const Identity &identity,
    CommitResult *result,
    QString *error)
{
    const auto current = record(identity);
    return saveLocal(
        identity,
        current ? current->rating : std::optional<double>{},
        std::nullopt,
        false,
        result,
        error);
}

bool RatingsReviewsStore::setSpoiler(
    const Identity &identity,
    bool spoiler,
    CommitResult *result,
    QString *error)
{
    const auto current = record(identity);
    if (!current || !current->review) {
        setError(error, QStringLiteral(
            "Canonical spoiler state requires a present review."));
        return false;
    }
    return saveLocal(
        identity, current->rating, current->review, spoiler, result, error);
}

bool RatingsReviewsStore::applySyncedPut(
    const QString &recordKey,
    const Record &recordValue,
    CommitResult *result,
    QString *error)
{
    if (error)
        error->clear();
    if (!requireHealthy(error))
        return false;

    const QJsonObject encoded = recordToJson(recordValue);
    Record validated;
    if (!parseRecord(recordKey, encoded, &validated, error))
        return false;

    const auto current = recordByKey(recordKey);
    if (current && recordsEqual(*current, validated)
        && !m_tombstones.contains(recordKey)) {
        if (result) {
            result->changed = false;
            result->revision = m_revision;
            result->recordKey = recordKey;
        }
        return true;
    }

    QJsonObject candidateRecords = m_records;

    QJsonObject candidateTombstones = m_tombstones;
    candidateRecords.insert(recordKey, encoded);
    candidateTombstones.remove(recordKey);
    return commitCandidate(
        candidateRecords, candidateTombstones, recordKey,
        MutationOrigin::Remote, result, error);
}

bool RatingsReviewsStore::applySyncedDelete(
    const QString &recordKey,
    qint64 deletedAtMs,
    CommitResult *result,
    QString *error)
{
    if (error)
        error->clear();
    if (!requireHealthy(error))
        return false;
    if (!validRecordKey(recordKey) || deletedAtMs <= 0) {
        setError(error, QStringLiteral(
            "Synced ratings/reviews delete requires a canonical key and positive timestamp."));
        return false;
    }

    const QJsonObject encoded = tombstoneToJson(deletedAtMs);
    if (!m_records.contains(recordKey)
        && m_tombstones.value(recordKey).toObject() == encoded) {

        if (result) {
            result->changed = false;
            result->revision = m_revision;
            result->recordKey = recordKey;
        }
        return true;
    }

    QJsonObject candidateRecords = m_records;
    QJsonObject candidateTombstones = m_tombstones;
    candidateRecords.remove(recordKey);
    candidateTombstones.insert(recordKey, encoded);
    return commitCandidate(
        candidateRecords, candidateTombstones, recordKey,
        MutationOrigin::Remote, result, error);
}

bool RatingsReviewsStore::restoreCanonicalState(
    const QJsonObject &records,
    const QJsonObject &tombstones,
    CommitResult *result,
    QString *error)
{
    if (error)
        error->clear();
    if (!requireHealthy(error)
        || !validateCanonicalMaps(records, tombstones, error)) {

        return false;
    }

    return commitCandidate(
        records, tombstones, QString(),
        MutationOrigin::Restore, result, error);
}

bool RatingsReviewsStore::clearForProfileRetirement(QString *error)
{
    if (error)
        error->clear();
    if (!requireHealthy(error))
        return false;
    if (m_records.isEmpty() && m_tombstones.isEmpty())
        return true;

    return commitCandidate(
        {}, {}, QString(),
        MutationOrigin::Retirement, nullptr, error);
}

bool RatingsReviewsStore::flush(QString *error) const
{
    if (error)
        error->clear();
    return requireHealthy(error);
}

bool RatingsReviewsStore::mergeCanonicalState(
    const QJsonObject &accountRecords,
    const QJsonObject &accountTombstones,
    const QJsonObject &localRecords,
    const QJsonObject &localTombstones,
    QJsonObject *mergedRecords,
    QJsonObject *mergedTombstones,
    QString *error)
{
    if (error)
        error->clear();
    if (!mergedRecords || !mergedTombstones) {
        setError(error, QStringLiteral(
            "Ratings/reviews merge requires both output maps."));
        return false;
    }
    if (!validateCanonicalMaps(accountRecords, accountTombstones, error)
        || !validateCanonicalMaps(localRecords, localTombstones, error)) {
        return false;
    }

    QSet<QString> keys;
    for (auto it = accountRecords.constBegin(); it != accountRecords.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = accountTombstones.constBegin(); it != accountTombstones.constEnd(); ++it)
        keys.insert(it.key());

    for (auto it = localRecords.constBegin(); it != localRecords.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = localTombstones.constBegin(); it != localTombstones.constEnd(); ++it)
        keys.insert(it.key());

    QJsonObject outputRecords;
    QJsonObject outputTombstones;

    for (const QString &key : keys) {
        const bool accountLive = accountRecords.contains(key);
        const bool accountDeleted = accountTombstones.contains(key);
        const bool localLive = localRecords.contains(key);
        const bool localDeleted = localTombstones.contains(key);

        if (!accountLive && !accountDeleted) {
            if (localLive)
                outputRecords.insert(key, localRecords.value(key));
            else if (localDeleted)
                outputTombstones.insert(key, localTombstones.value(key));
            continue;
        }
        if (!localLive && !localDeleted) {
            if (accountLive)
                outputRecords.insert(key, accountRecords.value(key));

            else
                outputTombstones.insert(key, accountTombstones.value(key));
            continue;
        }

        const qint64 accountTime =
            accountLive
                ? accountRecords.value(key).toObject()
                      .value(QStringLiteral("updated_at_ms")).toInteger()
                : accountTombstones.value(key).toObject()
                      .value(QStringLiteral("deleted_at_ms")).toInteger();
        const qint64 localTime =
            localLive
                ? localRecords.value(key).toObject()
                      .value(QStringLiteral("updated_at_ms")).toInteger()
                : localTombstones.value(key).toObject()
                      .value(QStringLiteral("deleted_at_ms")).toInteger();

        if (accountTime > localTime) {
            if (accountLive)
                outputRecords.insert(key, accountRecords.value(key));
            else
                outputTombstones.insert(key, accountTombstones.value(key));
            continue;
        }

        if (localTime > accountTime) {
            if (localLive)
                outputRecords.insert(key, localRecords.value(key));
            else
                outputTombstones.insert(key, localTombstones.value(key));
            continue;
        }

        if (accountDeleted || localDeleted) {
            const QJsonValue winner =
                accountDeleted
                    ? accountTombstones.value(key)
                    : localTombstones.value(key);
            outputTombstones.insert(key, winner);
            continue;
        }

        const QJsonObject accountRecord =
            accountRecords.value(key).toObject();
        const QJsonObject localRecord =
            localRecords.value(key).toObject();
        const QJsonObject winner =
            mergeTieDigest(accountRecord) >= mergeTieDigest(localRecord)
                ? accountRecord
                : localRecord;
        outputRecords.insert(key, winner);
    }

    *mergedRecords = outputRecords;
    *mergedTombstones = outputTombstones;
    return true;
}
