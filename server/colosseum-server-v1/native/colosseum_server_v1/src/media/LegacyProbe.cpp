#include "server1/media/Probe.h"
#include "server1/ports/ProcessPort.h"
#include <QJsonArray>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <map>
#include <utility>
namespace server1::media {
QJsonObject LegacyProbe::normalize(const QByteArray& data)
{
    QJsonObject result;
    QJsonArray streams;
    bool parsing = false;
    static const QRegularExpression commas("\\s*,\\s*"),
        dur("Duration: (\\d\\d):(\\d\\d):(\\d\\d(\\.\\d\\d)?)",
            QRegularExpression::CaseInsensitiveOption),
        bits("bitrate: (\\d+)", QRegularExpression::CaseInsensitiveOption),
        stream("Stream #0:(\\d\\d?)(?:\\((\\w+)\\))?"), colons("\\s*:\\s*"), codec(" |,"),
        dims("([0-9]{3,4})x([0-9]{3,4})"), fps("([0-9]{2})(\\.)?([0-9]{2})? fps"),
        rate("([0-9]{3,4}) kb/s");
    for (auto line : QString::fromUtf8(data).split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if (line.contains("Input #0")) {
            parsing = true;
            auto containers = line.toLower().split(commas);
            if (containers.size() > 2) {
                containers.removeFirst();
                containers.removeLast();
                result["container"] = containers.contains("mp4") ? "mp4"
                    : containers.contains("matroska")            ? "matroska"
                                                                 : containers.first();
            }
        } else if (parsing && line[0] != ' ')
            parsing = false;
        else if (parsing) {
            auto d = dur.match(line);
            if (d.hasMatch()) {
                result["duration"] = 3600000 * d.captured(1).toDouble()
                    + 60000 * d.captured(2).toDouble() + 1000 * d.captured(3).toDouble();
                auto b = bits.match(line);
                if (b.hasMatch())
                    result["bitrate"] = 1000 * b.captured(1).toDouble();
            }
            auto s = stream.match(line);
            if (s.hasMatch()) {
                auto parts = line.split(colons);
                if (parts.size() < 4)
                    continue;
                auto codecs = parts[3].split(codec);
                auto dim = dims.match(parts[3]), f = fps.match(line), b = rate.match(line);
                int index = s.captured(1).toInt();
                QJsonValue size = QJsonValue::Null;
                if (dim.hasMatch())
                    size = QJsonArray { dim.captured(1).toInt(), dim.captured(2).toInt() };
                QJsonObject track { { "codec_type", parts[2].toLower() },
                    { "codec_name", codecs.first().toLower() }, { "size", size },
                    { "stream", index }, { "default", codecs.contains("(default)") },
                    { "bitrate",
                        b.hasMatch() ? QJsonValue(1000 * b.captured(1).toDouble())
                                     : QJsonValue(QJsonValue::Null) },
                    { "fps",
                        f.hasMatch()
                            ? QJsonValue((f.captured(1) + f.captured(2) + f.captured(3)).toDouble())
                            : QJsonValue(QJsonValue::Null) } };
                if (s.capturedStart(2) >= 0)
                    track["lang"] = s.captured(2);
                // JS array keys with a leading zero are non-index properties and vanish
                // in JSON.
                if (QString::number(index) == s.captured(1)) {
                    while (streams.size() <= index)
                        streams.append(QJsonValue::Null);
                    streams[index] = track;
                }
            }
        }
    }
    result["streams"] = streams;
    return result;
}
struct LegacyProbe::Impl {
    struct Pending {
        QByteArray data;
        Probe::Callback callback;
    };
    QString executable;
    ProcessPort port;
    std::map<quint64, Pending> pending;
    explicit Impl(QString e)
        : executable(std::move(e)) { };
};
LegacyProbe::LegacyProbe(QString executable, QObject* parent)
    : QObject(parent)
    , impl_(new Impl(std::move(executable)))
{
}
LegacyProbe::~LegacyProbe() = default;
quint64 LegacyProbe::start(const QString& url, Probe::Callback callback)
{
    if (impl_->executable.isEmpty()) {
        if (callback)
            callback({ { }, { { "message", "no ffmpeg found; call HLS.locateExecutables()" } } });
        return 0;
    }
    auto id = std::make_shared<quint64>(0);
    QPointer<LegacyProbe> guard(this);
    ProcessCallbacks callbacks;
    callbacks.onStderr = [guard, id](const QByteArray& bytes) {
        if (guard) {
            auto it = guard->impl_->pending.find(*id);
            if (it != guard->impl_->pending.end())
                it->second.data += bytes;
        }
    };
    callbacks.onOutputClosed = [guard, id] {
        if (!guard)
            return;
        auto& all = guard->impl_->pending;
        auto it = all.find(*id);
        if (it == all.end())
            return;
        auto pending = std::move(it->second);
        all.erase(it);
        ProbeResult r;
        r.value = normalize(pending.data);
        if (pending.callback)
            pending.callback(r);
    };
    callbacks.onFinished = [guard, id](const ProcessResult& p) {
        if (!guard)
            return;
        auto& all = guard->impl_->pending;
        auto it = all.find(*id);
        if (it == all.end())
            return;
        auto pending = std::move(it->second);
        all.erase(it);
        ProbeResult r;
        r.value = normalize(pending.data);
        if (!r.value["duration"].toDouble()) {
            if (!p.started)
                r.error = { { "message", p.errorString } };
            else if (p.exitCode != 0)
                r.error = { { "code",
                                p.exitStatus == QProcess::CrashExit ? QJsonValue(QJsonValue::Null)
                                                                    : QJsonValue(p.exitCode) },
                    { "message",
                        QString("process exited with bad code: %1")
                            .arg(p.exitStatus == QProcess::CrashExit
                                    ? "null"
                                    : QString::number(p.exitCode)) } };
        }
        if (!r.ok())
            r.value = { };
        if (pending.callback)
            pending.callback(r);
    };
    *id = impl_->port.start(ProcessSpec(impl_->executable, { "-i", url }), std::move(callbacks));
    impl_->pending.emplace(*id, Impl::Pending { { }, std::move(callback) });
    QTimer::singleShot(20000, this, [guard, id] {
        if (!guard)
            return;
        auto& all = guard->impl_->pending;
        auto it = all.find(*id);
        if (it == all.end())
            return;
        auto callback = std::move(it->second.callback);
        all.erase(it);
        (void)guard->impl_->port.cancel(*id);
        if (callback)
            callback({ { }, { { "message", "cannot probe within 20000" }, { "httpCode", 504 } } });
    });
    return *id;
}
bool LegacyProbe::cancel(quint64 id) { return impl_->pending.count(id) && impl_->port.cancel(id); }
} // namespace server1::media
