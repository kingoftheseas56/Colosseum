#include "server1/media/Probe.h"
#include "server1/ports/ProcessPort.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
namespace server1::media {
namespace {
    QString jsString(const QJsonValue& v)
    {
        if (v.isString())
            return v.toString();
        if (v.isDouble()) {
            // JS Number::toString uses decimal from 1e-6 through values below 1e21.
            const double value = v.toDouble();
            char buffer[64];
            const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
            QString text = QString::fromLatin1(buffer, converted.ptr - buffer);
            const int exponentAt = text.indexOf('e');
            if (exponentAt >= 0 && std::abs(value) >= 1e-6 && std::abs(value) < 1e21) {
                const int exponent = text.mid(exponentAt + 1).toInt();
                QString digits = text.left(exponentAt);
                const bool negative = digits.startsWith('-');
                if (negative)
                    digits.remove(0, 1);
                int point = digits.indexOf('.');
                if (point < 0)
                    point = digits.size();
                digits.remove('.');
                point += exponent;
                if (point <= 0)
                    digits = "0." + QString(-point, '0') + digits;
                else if (point >= digits.size())
                    digits += QString(point - digits.size(), '0');
                else
                    digits.insert(point, '.');
                return negative ? "-" + digits : digits;
            }
            return text;
        }

        if (v.isBool())
            return v.toBool() ? "true" : "false";
        if (v.isNull())
            return "null";
        if (v.isUndefined())
            return "undefined";
        if (v.isArray()) {
            QStringList parts;
            for (const auto& x : v.toArray())
                parts << ((x.isNull() || x.isUndefined()) ? QString() : jsString(x));
            return parts.join(',');
        }
        return "[object Object]";
    }
    double number(const QJsonValue& v, bool integer = false)
    {
        static const QRegularExpression i("^[+-]?[0-9]+"),
            f("^[+-]?(?:Infinity|(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+"
              ")?)");
        auto m = (integer ? i : f).match(jsString(v).trimmed());
        if (!m.hasMatch())
            return std::numeric_limits<double>::quiet_NaN();
        auto s = m.captured();
        if (s.endsWith("Infinity"))
            return s.startsWith('-') ? -INFINITY : INFINITY;
        return s.toDouble();
    }
    QJsonDocument firstProbeDocument(const QByteArray& bytes, QJsonParseError* error)
    {
        auto document = QJsonDocument::fromJson(bytes, error);
        // JSONStream's once(data) consumer retains its first root value. Trailing
        // values or malformed trailing bytes do not replace an earlier result.
        if (error->error == QJsonParseError::GarbageAtEnd)
            document = QJsonDocument::fromJson(bytes.left(error->offset), error);
        return document;
    }
    QJsonValue finite(double n, QJsonValue fallback = QJsonValue(QJsonValue::Null))
    {
        return std::isfinite(n) ? QJsonValue(n) : fallback;
    }
    bool truthy(const QJsonValue& v)
    {
        return !v.isNull() && !v.isUndefined() && (!v.isBool() || v.toBool())
            && (!v.isDouble() || v.toDouble() != 0) && (!v.isString() || !v.toString().isEmpty());
    }
    QJsonValue rate(const QJsonValue& v)
    {
        return finite(number(truthy(v) ? v : QJsonValue(0), true));
    }
    QJsonValue stringOr(const QJsonValue& v, QJsonValue fallback = QJsonValue(QJsonValue::Null))
    {
        return v.isString() ? v : fallback;
    }
} // namespace
QJsonObject Probe::normalize(const QJsonObject& input)
{
    if (input.contains("streams") && !input["streams"].isArray())
        throw std::invalid_argument("streams.map is not a function");
    if (input.contains("format") && input["format"].isNull())
        throw std::invalid_argument("Cannot read properties of null");
    const auto format = input["format"].toObject();
    const auto streams = input["streams"].toArray();
    const auto duration = finite(number(format["duration"]));
    QJsonArray out;
    for (const auto& value : streams) {
        if (value.isNull())
            throw std::invalid_argument("Cannot read properties of null");
        const auto s = value.toObject();
        auto tags = s["tags"].toObject();
        int id = 0;
        for (const auto& candidate : streams) {
            const auto c = candidate.toObject();
            if (c["codec_type"] == s["codec_type"]) {
                if (c["index"] == s["index"])
                    break;
                ++id;
            }
        }
        auto track = s["codec_type"];
        QJsonObject t { { "id", id }, { "index", finite(number(s["index"], true), -1) },
            { "track", stringOr(track, "unknown") },
            { "codec", stringOr(s["codec_name"], "unknown") },
            { "streamBitRate", rate(s["bit_rate"]) },
            { "streamMaxBitRate", rate(s["max_bit_rate"]) },
            { "startTime", finite(number(s["start_time"])) },
            { "startTimeTs", finite(number(s["start_pts"], true)) }, { "timescale", 1 } };
        if (s["time_base"].isString()) {
            auto base = s["time_base"].toString();
            t["timescale"] = finite(number(base.mid(base.indexOf('/') + 1), true), 1);
        }
        if (track == QStringLiteral("video")) {
            double fps = std::numeric_limits<double>::quiet_NaN();
            if (s["r_frame_rate"].isString()) {
                auto r = s["r_frame_rate"].toString();
                int slash = r.indexOf('/');
                fps = number(slash < 0 ? r.left(r.size() - 1) : r.left(slash), true)
                    / number(r.mid(slash + 1), true);
            }
            t.insert("width", finite(number(s["width"], true)));
            t.insert("height", finite(number(s["height"], true)));
            t.insert("frameRate", finite(fps));
            t.insert("numberOfFrames", finite(number(s["nb_frames"], true)));
            t.insert(
                "hasBFrames", s["has_b_frames"].isDouble() && s["has_b_frames"].toDouble() > 0);
            t.insert("isHdr",
                s["color_space"] == QStringLiteral("bt2020nc")
                    && s["color_transfer"] == QStringLiteral("smpte2084")
                    && s["color_primaries"] == QStringLiteral("bt2020"));
            t.insert("isDoVi", s["codec_tag_string"] == QStringLiteral("dvhe"));
            t.insert("formatBitRate", rate(format["bit_rate"]));
            t.insert("formatMaxBitRate", rate(format["max_bit_rate"]));
            t.insert("bps", rate(tags["BPS"]));
            t.insert("numberOfBytes", rate(tags["NUMBER_OF_BYTES"]));
            t.insert("formatDuration", duration);
        }
        if (track == QStringLiteral("audio")) {
            t.insert("sampleRate", finite(number(s["sample_rate"], true)));
            t.insert("channels", finite(number(s["channels"], true)));
            t.insert("channelLayout", stringOr(s["channel_layout"], "unknown"));
        }
        if (track == QStringLiteral("audio") || track == QStringLiteral("subtitle")) {
            t.insert("title", stringOr(tags["title"]));
            t.insert("language", stringOr(tags["language"]));
        }
        out.append(t);
    }
    return { { "format",
                 QJsonObject { { "name", stringOr(format["format_name"], "unknown") },
                     { "duration", duration } } },
        { "streams", out } };
}
QStringList Probe::arguments(const QString& url)
{
    return { "-show_entries",
        "stream=index,bit_rate,max_bit_rate,codec_type,codec_name,start_time,"
        "start_pts,r_frame_rate,sample_rate,channels,channel_layout,time_"
        "base,has_b_frames,nb_frames,width,height,color_space,color_transfer,"
        "color_primaries,codec_tag_string : "
        "stream_tags=title,language,duration,bps,number_of_bytes : "
        "format=format_name,duration,bit_rate,max_bit_rate",
        "-print_format", "json", url };
}
struct Probe::Impl {
    struct Pending {
        QByteArray data;
        Callback callback;
        QString url;
        bool bridgeReceived = false;
    };
    QString executable;
    RemoteProbeTransport remote;
    quint64 nextRemoteId = 1;
    ProcessPort port;
    std::map<quint64, Pending> pending;
    QList<QPair<QString, QJsonObject>> cache;
    explicit Impl(QString e)
        : executable(std::move(e))
    {
    }
};
Probe::Probe(QString executable, QObject* parent)
    : QObject(parent)
    , impl_(new Impl(std::move(executable)))
{
}
Probe::Probe(QString executable, RemoteProbeTransport remote, QObject* parent)
    : Probe(std::move(executable), parent)
{
    impl_->remote = std::move(remote);
}
Probe::~Probe() = default;
bool Probe::contains(const QString& url) const
{
    for (const auto& entry : impl_->cache)
        if (entry.first == url)
            return true;
    return false;
}
quint64 Probe::start(const QString& url, Callback callback, int timeoutMs)
{
    for (const auto& entry : impl_->cache)
        if (entry.first == url) {
            ProbeResult r { entry.second, { }, true };
            QTimer::singleShot(0, this, [callback = std::move(callback), r] {
                if (callback)
                    callback(r);
            });
            return 0;
        }
    if (impl_->remote.dispatch) {
        const auto id = impl_->nextRemoteId++;
        impl_->pending.emplace(id, Impl::Pending { { }, std::move(callback), url });
        QPointer<Probe> guard(this);
        const auto dispatch = impl_->remote.dispatch;
        dispatch(QUuid::createUuid().toString(QUuid::WithoutBraces), arguments(url),
            [guard, id](const RemoteProbeResult& result) {
                if (!guard)
                    return;
                auto& all = guard->impl_->pending;
                auto it = all.find(id);
                if (it == all.end() || it->second.bridgeReceived)
                    return;
                it->second.bridgeReceived = true;
                if (!result.error.isEmpty()) {
                    auto callback = std::move(it->second.callback);
                    all.erase(it);
                    if (callback)
                        callback({ { }, { { "message", result.error } } });
                    return;
                }
                it->second.data = result.data;
                // M861 create resolves only after bridge result; its close is 500ms
                // later. destroy in remote mode is intentionally a no-op, including
                // probe timeout.
                QTimer::singleShot(500, guard, [guard, id] {
                    if (!guard)
                        return;
                    auto& impl = *guard->impl_;
                    auto it = impl.pending.find(id);
                    if (it == impl.pending.end())
                        return;
                    auto p = std::move(it->second);
                    impl.pending.erase(it);
                    ProbeResult r;
                    QJsonParseError error;
                    auto doc = firstProbeDocument(p.data, &error);
                    if (error.error != QJsonParseError::NoError || !doc.isObject())
                        r.error = { { "message", "Probe process failed" } };
                    else
                        try {
                            r.value = normalize(doc.object());
                        } catch (const std::exception& e) {
                            r.error = { { "message", QString::fromUtf8(e.what()) } };
                        }
                    if (r.ok()) {
                        impl.cache.prepend({ p.url, r.value });
                        if (impl.cache.size() > 3)
                            impl.cache.removeLast();
                    }
                    if (p.callback)
                        p.callback(r);
                });
            });
        return id;
    }
    auto id = std::make_shared<quint64>(0);
    QPointer<Probe> guard(this);
    ProcessCallbacks callbacks;
    callbacks.onStdout = [guard, id](const QByteArray& bytes) {
        if (guard) {
            auto it = guard->impl_->pending.find(*id);
            if (it != guard->impl_->pending.end())
                it->second.data += bytes;
        }
    };
    callbacks.onFinished = [guard, id](const ProcessResult& p) {
        if (!guard)
            return;
        auto& impl = *guard->impl_;
        auto it = impl.pending.find(*id);
        if (it == impl.pending.end())
            return;
        auto pending = std::move(it->second);
        impl.pending.erase(it);
        ProbeResult r;
        QString error;
        if (!p.started)
            error = p.errorString;
        else if (p.exitStatus == QProcess::CrashExit || p.canceled)
            error = "Probe process has been terminated with signal: " + p.signal;
        else if (p.exitCode > 0)
            error = QString("Probe process exited with code: %1").arg(p.exitCode);
        else {
            QJsonParseError parse;
            auto doc = firstProbeDocument(pending.data, &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject())
                error = "Probe process failed";
            else
                try {
                    r.value = normalize(doc.object());
                } catch (const std::exception& e) {
                    error = QString::fromUtf8(e.what());
                }
        }
        if (!error.isEmpty())
            r.error = { { "message", error } };
        else {
            impl.cache.prepend({ pending.url, r.value });
            if (impl.cache.size() > 3)
                impl.cache.removeLast();
        }
        if (pending.callback)
            pending.callback(r);
    };
    *id = impl_->port.start(ProcessSpec(impl_->executable, arguments(url)), std::move(callbacks));
    impl_->pending.emplace(*id, Impl::Pending { { }, std::move(callback), url });
    QTimer::singleShot(std::max(0, timeoutMs), this, [guard, id] {
        if (guard && guard->impl_->pending.count(*id))
            guard->cancel(*id);
    });
    return *id;
}
bool Probe::cancel(quint64 id)
{
    return !impl_->remote.dispatch && impl_->pending.count(id) && impl_->port.cancel(id);
}
} // namespace server1::media
