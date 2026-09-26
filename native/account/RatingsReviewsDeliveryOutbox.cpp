#include "RatingsReviewsDeliveryOutbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace {
bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
}

RatingsReviewsDeliveryOutbox::RatingsReviewsDeliveryOutbox(const QString &path)
    : m_path(path)
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Ratings/Reviews outbox path is empty.");
    } else {
        load();
    }
}

bool RatingsReviewsDeliveryOutbox::healthy(QString *error) const
{
    if (!m_healthy && error)
        *error = m_persistenceError;
    return m_healthy;
}

QString RatingsReviewsDeliveryOutbox::persistenceError() const { return m_persistenceError; }
QList<RatingsReviewsDeliveryOperation> RatingsReviewsDeliveryOutbox::operations() const { return m_operations; }

std::optional<RatingsReviewsDeliveryOperation> RatingsReviewsDeliveryOutbox::operation(const QString &operationId) const
{
    for (const auto &operation : m_operations) {
        if (operation.operationId == operationId)
            return operation;
    }
    return std::nullopt;
}

bool RatingsReviewsDeliveryOutbox::append(
    const QList<RatingsReviewsDeliveryOperation> &operations,
    QString *error)
{
    QList<RatingsReviewsDeliveryOperation> candidate = m_operations;
    candidate.append(operations);
    return replaceAll(candidate, error);
}

bool RatingsReviewsDeliveryOutbox::replace(
    const RatingsReviewsDeliveryOperation &operation,
    QString *error)
{
    QList<RatingsReviewsDeliveryOperation> candidate = m_operations;
    for (auto &current : candidate) {
        if (current.operationId == operation.operationId) {
            current = operation;
            return replaceAll(candidate, error);
        }
    }
    return setError(error, QStringLiteral("Ratings/Reviews delivery operation does not exist."));
}

bool RatingsReviewsDeliveryOutbox::replaceAll(
    const QList<RatingsReviewsDeliveryOperation> &operations,
    QString *error)
{
    if (!healthy(error))
        return false;
    QSet<QString> ids;
    for (const auto &operation : operations) {
        QString parseError;
        if (!ratingsReviewsDeliveryOperationFromJson(
                ratingsReviewsDeliveryOperationToJson(operation), &parseError)) {
            return setError(error, parseError);
        }
        if (ids.contains(operation.operationId))
            return setError(error, QStringLiteral("Duplicate Ratings/Reviews delivery operation ID."));
        ids.insert(operation.operationId);
    }
    if (!persist(operations, error))
        return false;
    m_operations = operations;
    return true;
}

bool RatingsReviewsDeliveryOutbox::flush(QString *error) const
{
    return healthy(error) && persist(m_operations, error);
}

bool RatingsReviewsDeliveryOutbox::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Could not open Ratings/Reviews delivery outbox.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("version")).toInt() != 1
        || !document.object().value(QStringLiteral("operations")).isArray()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Ratings/Reviews delivery outbox is malformed.");
        return false;
    }
    QList<RatingsReviewsDeliveryOperation> loaded;
    QSet<QString> ids;
    for (const QJsonValue &value : document.object().value(QStringLiteral("operations")).toArray()) {
        QString error;
        const auto operation = value.isObject()
            ? ratingsReviewsDeliveryOperationFromJson(value.toObject(), &error) : std::nullopt;
        if (!operation || ids.contains(operation->operationId)) {
            m_healthy = false;
            m_persistenceError = error.isEmpty()
                ? QStringLiteral("Ratings/Reviews delivery outbox has duplicate or invalid operations.") : error;
            return false;
        }
        ids.insert(operation->operationId);
        loaded.append(*operation);
    }
    m_operations = loaded;
    return true;
}

bool RatingsReviewsDeliveryOutbox::persist(
    const QList<RatingsReviewsDeliveryOperation> &candidate,
    QString *error) const
{
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return setError(error, QStringLiteral("Could not create Ratings/Reviews delivery outbox directory."));
    QJsonArray array;
    for (const auto &operation : candidate)
        array.append(ratingsReviewsDeliveryOperationToJson(operation));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Could not write Ratings/Reviews delivery outbox."));
    file.write(QJsonDocument({
        {QStringLiteral("version"), 1},
        {QStringLiteral("operations"), array}}).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return setError(error, QStringLiteral("Could not atomically save Ratings/Reviews delivery outbox."));
    return true;
}
