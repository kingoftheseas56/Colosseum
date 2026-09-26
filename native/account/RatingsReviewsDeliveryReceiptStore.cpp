#include "RatingsReviewsDeliveryReceiptStore.h"

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

RatingsReviewsDeliveryReceiptStore::RatingsReviewsDeliveryReceiptStore(
    const QString &path)
    : m_path(path)
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Ratings/Reviews receipt path is empty.");
    } else {
        load();
    }
}

bool RatingsReviewsDeliveryReceiptStore::healthy(QString *error) const
{
    if (!m_healthy && error)
        *error = m_persistenceError;
    return m_healthy;
}

QString RatingsReviewsDeliveryReceiptStore::persistenceError() const { return m_persistenceError; }
QList<RatingsReviewsDeliveryReceipt> RatingsReviewsDeliveryReceiptStore::receipts() const { return m_receipts; }

std::optional<RatingsReviewsDeliveryReceipt> RatingsReviewsDeliveryReceiptStore::receipt(
    const QString &operationId) const
{
    for (const auto &receipt : m_receipts) {
        if (receipt.operationId == operationId)
            return receipt;
    }
    return std::nullopt;
}

bool RatingsReviewsDeliveryReceiptStore::upsert(
    const RatingsReviewsDeliveryReceipt &receipt,
    QString *error)
{
    QList<RatingsReviewsDeliveryReceipt> candidate = m_receipts;
    bool replaced = false;
    for (auto &current : candidate) {
        if (current.operationId == receipt.operationId) {
            current = receipt;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        candidate.append(receipt);
    return replaceAll(candidate, error);
}

bool RatingsReviewsDeliveryReceiptStore::replaceAll(
    const QList<RatingsReviewsDeliveryReceipt> &receipts,
    QString *error)
{
    if (!healthy(error))
        return false;
    QSet<QString> ids;
    for (const auto &receipt : receipts) {
        QString parseError;
        if (!ratingsReviewsDeliveryReceiptFromJson(
                ratingsReviewsDeliveryReceiptToJson(receipt), &parseError)) {
            return setError(error, parseError);
        }
        if (ids.contains(receipt.operationId))
            return setError(error, QStringLiteral("Duplicate Ratings/Reviews receipt operation ID."));
        ids.insert(receipt.operationId);
    }
    if (!persist(receipts, error))
        return false;
    m_receipts = receipts;
    return true;
}

bool RatingsReviewsDeliveryReceiptStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Could not open Ratings/Reviews receipts.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("version")).toInt() != 1
        || !document.object().value(QStringLiteral("receipts")).isArray()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Ratings/Reviews receipts are malformed.");
        return false;
    }
    QList<RatingsReviewsDeliveryReceipt> loaded;
    QSet<QString> ids;
    for (const QJsonValue &value : document.object().value(QStringLiteral("receipts")).toArray()) {
        QString error;
        const auto receipt = value.isObject()
            ? ratingsReviewsDeliveryReceiptFromJson(value.toObject(), &error) : std::nullopt;
        if (!receipt || ids.contains(receipt->operationId)) {
            m_healthy = false;
            m_persistenceError = error.isEmpty()
                ? QStringLiteral("Ratings/Reviews receipts contain duplicate or invalid entries.") : error;
            return false;
        }
        ids.insert(receipt->operationId);
        loaded.append(*receipt);
    }
    m_receipts = loaded;
    return true;
}

bool RatingsReviewsDeliveryReceiptStore::persist(
    const QList<RatingsReviewsDeliveryReceipt> &candidate,
    QString *error) const
{
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return setError(error, QStringLiteral("Could not create Ratings/Reviews receipt directory."));
    QJsonArray array;
    for (const auto &receipt : candidate)
        array.append(ratingsReviewsDeliveryReceiptToJson(receipt));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Could not write Ratings/Reviews receipts."));
    file.write(QJsonDocument({
        {QStringLiteral("version"), 1},
        {QStringLiteral("receipts"), array}}).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return setError(error, QStringLiteral("Could not atomically save Ratings/Reviews receipts."));
    return true;
}
