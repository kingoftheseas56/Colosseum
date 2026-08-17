// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "TankobanReaderPreferencesCodec.h"

#include "SyncPayloadFirewall.h"
#include "comicreader/ComicReaderRenderProfile.h"

#include <QByteArray>
#include <QJsonValue>
#include <QMetaType>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <cmath>

namespace {
constexpr auto kPrefix =
    "tankoban/preferences/";

const QStringList &topLevelPreferenceFields() {
    static const QStringList fields = {
        QStringLiteral("layout"),
        QStringLiteral("order"),
        QStringLiteral("zoomPercent"),
        QStringLiteral("stripWidthPct"),
        QStringLiteral("stripGap"),
        QStringLiteral("autoScrollSpeed")
    };
    return fields;
}

const QStringList &legacyPreferenceFields() {
    static const QStringList fields = {
        QStringLiteral("readingMode"),
        QStringLiteral("rm"),
        QStringLiteral("mode"),
        QStringLiteral("rtl"),
        QStringLiteral("sw"),
        QStringLiteral("sg")
    };
    return fields;
}

const QStringList &approvedRenderFields() {
    static const QStringList fields = {
        QStringLiteral("brightness"),
        QStringLiteral("contrast"),
        QStringLiteral("gamma"),
        QStringLiteral("rotation"),
        QStringLiteral("autoCrop"),
        QStringLiteral("nightFilter")
    };
    return fields;
}

bool validLayout(
    const QString &value) {
    return value
            == QStringLiteral("single_page")
        || value
            == QStringLiteral("paired_pages")
        || value
            == QStringLiteral("long_strip");
}

bool validOrder(
    const QString &value) {
    return value
            == QStringLiteral("ltr")
        || value
            == QStringLiteral("rtl");
}

QString legacyReadingMode(
    const QVariantMap &raw) {
    QString legacy =
        raw.value(
                QStringLiteral(
                    "readingMode"))
            .toString()
            .trimmed();

    if (legacy.isEmpty()) {
        legacy =
            raw.value(
                    QStringLiteral("rm"))
                .toString()
                .trimmed();
    }

    if (legacy.isEmpty()
        && raw.contains(
            QStringLiteral("mode"))) {
        const QString mode =
            raw.value(
                    QStringLiteral("mode"))
                .toString()
                .trimmed();

        if (mode
            == QStringLiteral("long_strip")) {
            legacy =
                QStringLiteral("strip");
        } else {
            const QVariant rtl =
                raw.value(
                    QStringLiteral("rtl"));
            if (rtl.isValid()
                && rtl.metaType().id()
                    == QMetaType::Bool) {
                legacy =
                    rtl.toBool()
                    ? QStringLiteral("manga")
                    : QStringLiteral("comic");
            }
        }
    }

    if (legacy
            != QStringLiteral("manga")
        && legacy
            != QStringLiteral("comic")
        && legacy
            != QStringLiteral("strip")) {
        // Tankoban is a Japanese-lineage lane.
        legacy =
            QStringLiteral("manga");
    }

    return legacy;
}

bool jsonNumberInRange(
    const QJsonValue &value,
    double low,
    double high) {
    if (!value.isDouble())
        return false;

    const double number =
        value.toDouble();

    return std::isfinite(number)
        && number >= low
        && number <= high;
}

bool jsonIntegerInRange(
    const QJsonValue &value,
    int low,
    int high) {
    if (!value.isDouble())
        return false;

    const double number =
        value.toDouble();
    if (!std::isfinite(number))
        return false;

    const double rounded =
        std::round(number);
    return qAbs(number - rounded)
            < 0.0000001
        && rounded >= low
        && rounded <= high;
}

bool exactKeys(
    const QJsonObject &object,
    const QSet<QString> &expected) {
    if (object.size()
        != expected.size()) {
        return false;
    }

    for (auto it =
             object.constBegin();
         it != object.constEnd();
         ++it) {
        if (!expected.contains(it.key()))
            return false;
    }

    return true;
}
}

bool TankobanReaderPreferencesCodec::
isPortableSeriesId(
    const QString &seriesId) {
    if (seriesId.isEmpty()
        || seriesId
            != seriesId.trimmed()) {
        return false;
    }

    if (SyncPayloadFirewall::
            isFilesystemPathValue(
                seriesId)) {
        return false;
    }

    return true;
}

QString TankobanReaderPreferencesCodec::
recordKey(
    const QString &seriesId) {
    if (!isPortableSeriesId(seriesId))
        return {};

    const QByteArray encoded =
        seriesId
            .toUtf8()
            .toBase64(
                QByteArray::Base64UrlEncoding
                | QByteArray::
                    OmitTrailingEquals);

    if (encoded.isEmpty())
        return {};

    return QString::fromLatin1(kPrefix)
        + QString::fromLatin1(encoded);
}

bool TankobanReaderPreferencesCodec::
decodeRecordKey(
    const QString &recordKeyValue,
    QString *seriesId) {
    if (!seriesId)
        return false;

    const QString prefix =
        QString::fromLatin1(kPrefix);
    if (!recordKeyValue.startsWith(prefix))
        return false;

    const QString encoded =
        recordKeyValue.mid(prefix.size());
    if (encoded.isEmpty()
        || encoded.contains(
            QLatin1Char('/'))) {
        return false;
    }

    const QByteArray decoded =
        QByteArray::fromBase64(
            encoded.toLatin1(),
            QByteArray::
                Base64UrlEncoding);

    const QString candidate =
        QString::fromUtf8(decoded);

    if (!isPortableSeriesId(candidate))
        return false;

    if (recordKey(candidate)
        != recordKeyValue) {
        return false;
    }

    *seriesId = candidate;
    return true;
}

bool TankobanReaderPreferencesCodec::
hasApprovedOpinion(
    const QVariantMap &rawRecord) {
    for (const QString &field :
         topLevelPreferenceFields()) {
        if (rawRecord.contains(field))
            return true;
    }

    for (const QString &field :
         legacyPreferenceFields()) {
        if (rawRecord.contains(field))
            return true;
    }

    const QVariant renderValue =
        rawRecord.value(
            QStringLiteral(
                "renderProfile"));
    if (renderValue.isValid()) {
        const QVariantMap render =
            renderValue.toMap();
        for (const QString &field :
             approvedRenderFields()) {
            if (render.contains(field))
                return true;
        }
    }

    return false;
}

bool TankobanReaderPreferencesCodec::
canonicalPayload(
    const QString &seriesId,
    const QVariantMap &rawRecord,
    QJsonObject *payload,
    QString *error) {
    if (!payload) {
        return fail(
            error,
            QStringLiteral(
                "A Tankoban preference payload output is required."));
    }

    if (!isPortableSeriesId(seriesId)) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban series id is not a portable logical identity."));
    }

    if (!hasApprovedOpinion(rawRecord)) {
        return fail(
            error,
            QStringLiteral(
                "The series record has no approved Tankoban reader preference opinion."));
    }

    const QString layout =
        canonicalLayout(rawRecord);
    const QString order =
        canonicalOrder(rawRecord);

    const double zoomPercent =
        clampedNumber(
            rawRecord,
            QStringLiteral(
                "zoomPercent"),
            QString(),
            100.0,
            260.0,
            100.0);

    const double stripWidth =
        clampedNumber(
            rawRecord,
            QStringLiteral(
                "stripWidthPct"),
            QStringLiteral("sw"),
            40.0,
            100.0,
            78.0);

    const double stripGap =
        clampedNumber(
            rawRecord,
            QStringLiteral(
                "stripGap"),
            QStringLiteral("sg"),
            0.0,
            80.0,
            0.0);

    const double autoScroll =
        clampedNumber(
            rawRecord,
            QStringLiteral(
                "autoScrollSpeed"),
            QString(),
            0.25,
            3.0,
            1.0);

    const QVariantMap rawRender =
        rawRecord
            .value(
                QStringLiteral(
                    "renderProfile"))
            .toMap();

    const comicreader::RenderProfile profile =
        comicreader::
            normalizeRenderProfile(
                rawRender);

    QJsonObject render;
    render.insert(
        QStringLiteral("brightness"),
        profile.brightness);
    render.insert(
        QStringLiteral("contrast"),
        profile.contrast);
    render.insert(
        QStringLiteral("gamma"),
        profile.gamma);
    render.insert(
        QStringLiteral("rotation"),
        profile.rotation);
    render.insert(
        QStringLiteral("autoCrop"),
        profile.autoCrop);
    render.insert(
        QStringLiteral("nightFilter"),
        profile.nightFilter);

    QJsonObject result;
    result.insert(
        QStringLiteral("seriesId"),
        seriesId);
    result.insert(
        QStringLiteral("layout"),
        layout);
    result.insert(
        QStringLiteral("order"),
        order);
    result.insert(
        QStringLiteral("zoomPercent"),
        zoomPercent);
    result.insert(
        QStringLiteral("stripWidthPct"),
        stripWidth);
    result.insert(
        QStringLiteral("stripGap"),
        stripGap);
    result.insert(
        QStringLiteral("autoScrollSpeed"),
        autoScroll);
    result.insert(
        QStringLiteral("renderProfile"),
        render);

    QString validationError;
    if (!validatePayload(
            result,
            seriesId,
            &validationError)) {
        return fail(
            error,
            validationError);
    }

    *payload = result;
    return true;
}

bool TankobanReaderPreferencesCodec::
validatePayload(
    const QJsonObject &payload,
    const QString &expectedSeriesId,
    QString *error) {
    static const QSet<QString>
        topKeys = {
            QStringLiteral("seriesId"),
            QStringLiteral("layout"),
            QStringLiteral("order"),
            QStringLiteral("zoomPercent"),
            QStringLiteral("stripWidthPct"),
            QStringLiteral("stripGap"),
            QStringLiteral("autoScrollSpeed"),
            QStringLiteral("renderProfile")
        };

    static const QSet<QString>
        renderKeys = {
            QStringLiteral("brightness"),
            QStringLiteral("contrast"),
            QStringLiteral("gamma"),
            QStringLiteral("rotation"),
            QStringLiteral("autoCrop"),
            QStringLiteral("nightFilter")
        };

    if (!exactKeys(payload, topKeys)) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban preference payload has unknown or missing schema-v1 fields."));
    }

    const QJsonValue seriesValue =
        payload.value(
            QStringLiteral("seriesId"));
    if (!seriesValue.isString()) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban preference seriesId must be a string."));
    }

    const QString seriesId =
        seriesValue.toString();
    if (!isPortableSeriesId(seriesId)
        || seriesId
            != expectedSeriesId) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban preference payload identity does not match its record key."));
    }

    const QString layout =
        payload.value(
                QStringLiteral("layout"))
            .toString();
    if (!payload
            .value(
                QStringLiteral("layout"))
            .isString()
        || !validLayout(layout)) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban preference layout is invalid."));
    }

    const QString order =
        payload.value(
                QStringLiteral("order"))
            .toString();
    if (!payload
            .value(
                QStringLiteral("order"))
            .isString()
        || !validOrder(order)) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban preference order is invalid."));
    }

    if (!jsonNumberInRange(
            payload.value(
                QStringLiteral(
                    "zoomPercent")),
            100.0,
            260.0)
        || !jsonNumberInRange(
            payload.value(
                QStringLiteral(
                    "stripWidthPct")),
            40.0,
            100.0)
        || !jsonNumberInRange(
            payload.value(
                QStringLiteral(
                    "stripGap")),
            0.0,
            80.0)
        || !jsonNumberInRange(
            payload.value(
                QStringLiteral(
                    "autoScrollSpeed")),
            0.25,
            3.0)) {
        return fail(
            error,
            QStringLiteral(
                "A Tankoban numeric reader preference is outside schema-v1 bounds."));
    }

    const QJsonValue renderValue =
        payload.value(
            QStringLiteral(
                "renderProfile"));
    if (!renderValue.isObject()) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban render profile must be an object."));
    }

    const QJsonObject render =
        renderValue.toObject();
    if (!exactKeys(render, renderKeys)) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban render profile has unknown or missing schema-v1 fields."));
    }

    if (!jsonIntegerInRange(
            render.value(
                QStringLiteral(
                    "brightness")),
            -100,
            100)
        || !jsonIntegerInRange(
            render.value(
                QStringLiteral(
                    "contrast")),
            -100,
            100)
        || !jsonIntegerInRange(
            render.value(
                QStringLiteral(
                    "gamma")),
            10,
            300)
        || !jsonIntegerInRange(
            render.value(
                QStringLiteral(
                    "rotation")),
            0,
            270)) {
        return fail(
            error,
            QStringLiteral(
                "A Tankoban render-profile number is invalid."));
    }

    const int rotation =
        render.value(
                QStringLiteral(
                    "rotation"))
            .toInt();
    if (rotation != 0
        && rotation != 90
        && rotation != 180
        && rotation != 270) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban rotation must be a quarter turn."));
    }

    if (!render
            .value(
                QStringLiteral(
                    "autoCrop"))
            .isBool()
        || !render
                .value(
                    QStringLiteral(
                        "nightFilter"))
                .isBool()) {
        return fail(
            error,
            QStringLiteral(
                "Tankoban crop and night-treatment fields must be boolean."));
    }

    return true;
}

bool TankobanReaderPreferencesCodec::
overlaySyncedPayload(
    const QVariantMap &existingRaw,
    const QJsonObject &payload,
    QVariantMap *nextRaw,
    QString *error) {
    if (!nextRaw) {
        return fail(
            error,
            QStringLiteral(
                "A Tankoban raw-record output is required."));
    }

    const QString seriesId =
        payload.value(
                QStringLiteral(
                    "seriesId"))
            .toString();

    QString validationError;
    if (!validatePayload(
            payload,
            seriesId,
            &validationError)) {
        return fail(
            error,
            validationError);
    }

    QVariantMap next =
        existingRaw;

    for (const QString &field :
         legacyPreferenceFields()) {
        next.remove(field);
    }

    for (const QString &field :
         topLevelPreferenceFields()) {
        next.insert(
            field,
            payload.value(field)
                .toVariant());
    }

    QVariantMap render =
        next.value(
                QStringLiteral(
                    "renderProfile"))
            .toMap();

    for (const QString &field :
         approvedRenderFields()) {
        render.insert(
            field,
            payload
                .value(
                    QStringLiteral(
                        "renderProfile"))
                .toObject()
                .value(field)
                .toVariant());
    }

    next.insert(
        QStringLiteral("renderProfile"),
        render);

    *nextRaw = next;
    return true;
}

QVariantMap TankobanReaderPreferencesCodec::
clearSyncedFields(
    const QVariantMap &existingRaw) {
    QVariantMap next =
        existingRaw;

    for (const QString &field :
         topLevelPreferenceFields()) {
        next.remove(field);
    }

    for (const QString &field :
         legacyPreferenceFields()) {
        next.remove(field);
    }

    QVariantMap render =
        next.value(
                QStringLiteral(
                    "renderProfile"))
            .toMap();

    for (const QString &field :
         approvedRenderFields()) {
        render.remove(field);
    }

    if (render.isEmpty()) {
        next.remove(
            QStringLiteral(
                "renderProfile"));
    } else {
        next.insert(
            QStringLiteral(
                "renderProfile"),
            render);
    }

    return next;
}

QString TankobanReaderPreferencesCodec::
canonicalLayout(
    const QVariantMap &rawRecord) {
    const QString explicitLayout =
        rawRecord
            .value(
                QStringLiteral("layout"))
            .toString()
            .trimmed();

    if (validLayout(explicitLayout))
        return explicitLayout;

    return legacyReadingMode(rawRecord)
            == QStringLiteral("strip")
        ? QStringLiteral("long_strip")
        : QStringLiteral("paired_pages");
}

QString TankobanReaderPreferencesCodec::
canonicalOrder(
    const QVariantMap &rawRecord) {
    const QString explicitOrder =
        rawRecord
            .value(
                QStringLiteral("order"))
            .toString()
            .trimmed();

    if (validOrder(explicitOrder))
        return explicitOrder;

    const QString legacy =
        legacyReadingMode(rawRecord);

    if (legacy
        == QStringLiteral("manga")) {
        return QStringLiteral("rtl");
    }

    if (legacy
        == QStringLiteral("comic")) {
        return QStringLiteral("ltr");
    }

    const QVariant rtl =
        rawRecord.value(
            QStringLiteral("rtl"));
    if (rtl.isValid()
        && rtl.metaType().id()
            == QMetaType::Bool) {
        return rtl.toBool()
            ? QStringLiteral("rtl")
            : QStringLiteral("ltr");
    }

    // Tankoban default direction.
    return QStringLiteral("rtl");
}

double TankobanReaderPreferencesCodec::
clampedNumber(
    const QVariantMap &rawRecord,
    const QString &field,
    const QString &legacyField,
    double low,
    double high,
    double fallback) {
    QVariant value =
        rawRecord.value(field);

    if ((!value.isValid()
         || value.isNull())
        && !legacyField.isEmpty()) {
        value =
            rawRecord.value(
                legacyField);
    }

    bool ok = false;
    double number =
        value.toDouble(&ok);

    if (!ok
        || !std::isfinite(number)
        || number == 0.0) {
        number = fallback;
    }

    return qBound(
        low,
        number,
        high);
}

bool TankobanReaderPreferencesCodec::fail(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
