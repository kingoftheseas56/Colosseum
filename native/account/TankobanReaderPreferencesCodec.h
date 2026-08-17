#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

class TankobanReaderPreferencesCodec {
public:
    static constexpr int kSchemaVersion = 1;

    static bool isPortableSeriesId(
        const QString &seriesId);

    static QString recordKey(
        const QString &seriesId);

    static bool decodeRecordKey(
        const QString &recordKey,
        QString *seriesId);

    // True only when the raw series record carries at least one approved
    // Slice-15 preference opinion. renderProfile.quality by itself does not
    // qualify because that field is still product-decision-gated.
    static bool hasApprovedOpinion(
        const QVariantMap &rawRecord);

    // Converts legacy/current reader storage into the complete canonical
    // schema-v1 payload frozen by Bundle 8A.
    static bool canonicalPayload(
        const QString &seriesId,
        const QVariantMap &rawRecord,
        QJsonObject *payload,
        QString *error = nullptr);

    // Strict schema validation. Unknown fields fail closed rather than
    // silently expanding schema v1.
    static bool validatePayload(
        const QJsonObject &payload,
        const QString &expectedSeriesId,
        QString *error = nullptr);

    // Applies a canonical remote PUT over the current raw owner record while
    // preserving unrelated/local/future fields, including render quality.
    static bool overlaySyncedPayload(
        const QVariantMap &existingRaw,
        const QJsonObject &payload,
        QVariantMap *nextRaw,
        QString *error = nullptr);

    // Removes every schema-v1/current/legacy synced preference field while
    // preserving unrelated fields and product-decision-gated render quality.
    static QVariantMap clearSyncedFields(
        const QVariantMap &existingRaw);

private:
    static QString canonicalLayout(
        const QVariantMap &rawRecord);

    static QString canonicalOrder(
        const QVariantMap &rawRecord);

    static double clampedNumber(
        const QVariantMap &rawRecord,
        const QString &field,
        const QString &legacyField,
        double low,
        double high,
        double fallback);

    static bool fail(
        QString *error,
        const QString &message);
};
