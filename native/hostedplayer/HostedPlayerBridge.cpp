// HostedPlayerBridge.cpp — see HostedPlayerBridge.h for the security contract.

#include "HostedPlayerBridge.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>

namespace {
// A hosted PLAYER_EVENT payload is tiny; anything larger is not something VidKing
// sends, so cap it before we even parse.
constexpr int kMaxPayloadBytes = 4096;
// 24 hours — no legitimate title runs longer; a larger duration is a bad payload.
constexpr double kMaxDurationSecs = 24.0 * 60.0 * 60.0;
// mpv-parity slack: a currentTime a few seconds past duration is a rounding artifact,
// not an attack; further past that is impossible and dropped.
constexpr double kCurrentTimeToleranceSecs = 5.0;

bool isAllowedEvent(const QString &e)
{
    return e == QLatin1String("play") || e == QLatin1String("playing")
        || e == QLatin1String("pause") || e == QLatin1String("timeupdate")
        || e == QLatin1String("seeked") || e == QLatin1String("ended")
        || e == QLatin1String("error");
}

// A numeric field is valid iff absent (→ 0) or a finite, non-negative number. Returns
// false when the key is present but not a finite >= 0 number, so the whole event drops.
bool readNum(const QJsonObject &o, QLatin1String key, double &out)
{
    if (!o.contains(key)) { out = 0.0; return true; }
    const QJsonValue v = o.value(key);
    if (!v.isDouble()) return false;
    const double n = v.toDouble();
    if (!std::isfinite(n) || n < 0.0) return false;
    out = n;
    return true;
}
} // namespace

void HostedPlayerBridge::postPlayerEvent(const QString &json)
{
    const QByteArray bytes = json.toUtf8();
    if (bytes.size() > kMaxPayloadBytes)
        return;                                          // oversize → drop before parse

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;                                          // malformed / non-object → drop
    const QJsonObject o = doc.object();

    const QString event = o.value(QStringLiteral("event")).toString();
    if (!isAllowedEvent(event))
        return;                                          // unknown event name → drop

    double currentTime = 0.0, duration = 0.0, progress = 0.0;
    if (!readNum(o, QLatin1String("currentTime"), currentTime)) return;
    if (!readNum(o, QLatin1String("duration"), duration)) return;
    if (!readNum(o, QLatin1String("progress"), progress)) return;

    if (duration > kMaxDurationSecs)
        return;                                          // absurd duration → drop
    if (duration > 0.0 && currentTime > duration + kCurrentTimeToleranceSecs)
        return;                                          // impossible position → drop

    // Copy ONLY the allowlisted fields. Nothing the iframe puts in an unexpected key
    // ever reaches QML.
    QVariantMap out;
    out.insert(QStringLiteral("event"), event);
    out.insert(QStringLiteral("currentTime"), currentTime);
    out.insert(QStringLiteral("duration"), duration);
    out.insert(QStringLiteral("progress"), progress);
    out.insert(QStringLiteral("session"), o.value(QStringLiteral("session")).toString());
    out.insert(QStringLiteral("id"), o.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("mediaType"), o.value(QStringLiteral("mediaType")).toString());
    out.insert(QStringLiteral("season"), o.value(QStringLiteral("season")).toInt());
    out.insert(QStringLiteral("episode"), o.value(QStringLiteral("episode")).toInt());

    emit playerEvent(out);
}
