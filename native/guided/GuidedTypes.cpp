// native/guided/GuidedTypes.cpp
#include "guided/GuidedTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace guided {

// --- NormalizedRect geometry -------------------------------------------------

bool NormalizedRect::isValid() const {
    constexpr double kEps = 1e-6;
    return width > 0.0 && height > 0.0
        && x >= -kEps && y >= -kEps
        && x + width <= 1.0 + kEps
        && y + height <= 1.0 + kEps;
}

bool NormalizedRect::contains(NormalizedPoint point) const {
    return point.x >= x && point.x <= x + width
        && point.y >= y && point.y <= y + height;
}

NormalizedPoint NormalizedRect::center() const {
    return {x + width / 2.0, y + height / 2.0};
}

double NormalizedRect::area() const {
    return width * height;
}

// --- stable string codes -----------------------------------------------------

QString toCode(FallbackCode code) {
    switch (code) {
    case FallbackCode::None:                return QStringLiteral("none");
    case FallbackCode::NoPanels:            return QStringLiteral("no_panels");
    case FallbackCode::LayoutAmbiguous:     return QStringLiteral("layout_ambiguous");
    case FallbackCode::SpreadUncertain:     return QStringLiteral("spread_uncertain");
    case FallbackCode::ImageDecodeFailed:   return QStringLiteral("image_decode_failed");
    case FallbackCode::ModelMissing:        return QStringLiteral("model_missing");
    case FallbackCode::ModelChecksumFailed: return QStringLiteral("model_checksum_failed");
    case FallbackCode::InferenceFailed:     return QStringLiteral("inference_failed");
    case FallbackCode::StoreFailed:         return QStringLiteral("store_failed");
    }
    return QStringLiteral("none");
}

namespace {

FallbackCode fallbackFromCode(const QString& code) {
    if (code == QLatin1String("no_panels")) return FallbackCode::NoPanels;
    if (code == QLatin1String("layout_ambiguous")) return FallbackCode::LayoutAmbiguous;
    if (code == QLatin1String("spread_uncertain")) return FallbackCode::SpreadUncertain;
    if (code == QLatin1String("image_decode_failed")) return FallbackCode::ImageDecodeFailed;
    if (code == QLatin1String("model_missing")) return FallbackCode::ModelMissing;
    if (code == QLatin1String("model_checksum_failed")) return FallbackCode::ModelChecksumFailed;
    if (code == QLatin1String("inference_failed")) return FallbackCode::InferenceFailed;
    if (code == QLatin1String("store_failed")) return FallbackCode::StoreFailed;
    return FallbackCode::None;
}

QString outcomeToString(PlanOutcome outcome) {
    switch (outcome) {
    case PlanOutcome::Trusted:  return QStringLiteral("trusted");
    case PlanOutcome::Fallback: return QStringLiteral("fallback");
    case PlanOutcome::Failed:   return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

PlanOutcome outcomeFromString(const QString& s) {
    if (s == QLatin1String("trusted")) return PlanOutcome::Trusted;
    if (s == QLatin1String("fallback")) return PlanOutcome::Fallback;
    return PlanOutcome::Failed;
}

QString stepKindToString(StepKind kind) {
    switch (kind) {
    case StepKind::Overview:     return QStringLiteral("overview");
    case StepKind::Panel:        return QStringLiteral("panel");
    case StepKind::InternalStop: return QStringLiteral("internal_stop");
    }
    return QStringLiteral("overview");
}

StepKind stepKindFromString(const QString& s) {
    if (s == QLatin1String("panel")) return StepKind::Panel;
    if (s == QLatin1String("internal_stop")) return StepKind::InternalStop;
    return StepKind::Overview;
}

QJsonObject rectToJson(const NormalizedRect& rect) {
    return QJsonObject{
        {QStringLiteral("x"), rect.x},
        {QStringLiteral("y"), rect.y},
        {QStringLiteral("w"), rect.width},
        {QStringLiteral("h"), rect.height},
    };
}

NormalizedRect rectFromJson(const QJsonObject& obj) {
    NormalizedRect rect;
    rect.x = obj.value(QStringLiteral("x")).toDouble();
    rect.y = obj.value(QStringLiteral("y")).toDouble();
    rect.width = obj.value(QStringLiteral("w")).toDouble();
    rect.height = obj.value(QStringLiteral("h")).toDouble();
    return rect;
}

} // namespace

QByteArray serializePath(const GuidedPath& path) {
    QJsonArray steps;
    for (const PathStep& step : path.steps) {
        steps.append(QJsonObject{
            {QStringLiteral("kind"), stepKindToString(step.kind)},
            {QStringLiteral("sourcePanelId"), step.sourcePanelId},
            {QStringLiteral("camera"), rectToJson(step.camera)},
            {QStringLiteral("holdSecondsAt1x"), step.holdSecondsAt1x},
            {QStringLiteral("transitionSecondsAt1x"), step.transitionSecondsAt1x},
            {QStringLiteral("plannerConfidence"), step.plannerConfidence},
        });
    }
    const QJsonObject root{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("canvasFingerprint"), path.canvasFingerprint},
        {QStringLiteral("modelVersion"), path.modelVersion},
        {QStringLiteral("plannerVersion"), path.plannerVersion},
        {QStringLiteral("outcome"), outcomeToString(path.outcome)},
        {QStringLiteral("reason"), toCode(path.reason)},
        {QStringLiteral("steps"), steps},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<GuidedPath> deserializePath(const QByteArray& bytes) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;

    const QJsonObject root = doc.object();
    // Reject anything but schema 1 so a future bump can never be misread as v1.
    if (root.value(QStringLiteral("schema")).toInt(0) != 1)
        return std::nullopt;
    if (!root.value(QStringLiteral("steps")).isArray())
        return std::nullopt;

    GuidedPath path;
    path.canvasFingerprint = root.value(QStringLiteral("canvasFingerprint")).toString();
    path.modelVersion = root.value(QStringLiteral("modelVersion")).toString();
    path.plannerVersion = root.value(QStringLiteral("plannerVersion")).toString();
    path.outcome = outcomeFromString(root.value(QStringLiteral("outcome")).toString());
    path.reason = fallbackFromCode(root.value(QStringLiteral("reason")).toString());

    const QJsonArray steps = root.value(QStringLiteral("steps")).toArray();
    path.steps.reserve(steps.size());
    for (const QJsonValue& value : steps) {
        if (!value.isObject())
            return std::nullopt;
        const QJsonObject obj = value.toObject();
        PathStep step;
        step.kind = stepKindFromString(obj.value(QStringLiteral("kind")).toString());
        step.sourcePanelId = obj.value(QStringLiteral("sourcePanelId")).toInt(-1);
        step.camera = rectFromJson(obj.value(QStringLiteral("camera")).toObject());
        step.holdSecondsAt1x = obj.value(QStringLiteral("holdSecondsAt1x")).toDouble();
        step.transitionSecondsAt1x = obj.value(QStringLiteral("transitionSecondsAt1x")).toDouble();
        step.plannerConfidence = obj.value(QStringLiteral("plannerConfidence")).toDouble();
        path.steps.append(step);
    }
    return path;
}

} // namespace guided
