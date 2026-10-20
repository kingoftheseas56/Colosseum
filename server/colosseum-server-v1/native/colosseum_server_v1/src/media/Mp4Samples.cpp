#include "server1/media/KeyframeIndex.h"
#include <QJsonArray>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace server1::media {
namespace {
constexpr qsizetype MaxSamples = 10000000;
quint64 uintAt(const QByteArray& b, qsizetype p, int n)
{
    if (p < 0 || p > b.size() || n > b.size() - p)
        throw std::runtime_error("Offset is outside the bounds of the DataView");
    quint64 v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | quint8(b[p + i]);
    return v;
}
struct Box { QByteArray type, data; };
// mux.js inspect uses only the 32-bit size, including its size=1 behavior.
// Stream-header parsing below is deliberately separate: mp4-stream knows largesize.
QVector<Box> inspect(const QByteArray& b)
{
    QVector<Box> result;
    for (qsizetype p = 0; p < b.size();) {
        const auto size = uintAt(b, p, 4);
        const auto end = size > 1 ? qMin(quint64(b.size()), quint64(p) + size) : quint64(b.size());
        result.append({b.mid(p + 4, 4), b.mid(p + 8, qMax<qint64>(0, qint64(end) - p - 8))});
        if (end <= quint64(p)) throw std::runtime_error("invalid box size");
        p = qsizetype(end);
    }
    return result;
}
Box find(const QVector<Box>& boxes, const char* type, bool required = true)
{
    for (const auto& box : boxes) if (box.type == type) return box;
    if (required) throw std::runtime_error((QByteArray(type) + " atom missing").constData());
    return {};
}
QVector<double> table(const Box& box, bool composition)
{
    QVector<double> out;
    if (box.type.isEmpty()) return out;
    const auto count = uintAt(box.data, 4, 4);
    for (quint64 i = 0; i < count; ++i) {
        const auto n = uintAt(box.data, 8 + i * 8, 4);
        const auto raw = uintAt(box.data, 12 + i * 8, 4);
        const double value = composition && uintAt(box.data, 0, 1) != 0 ? double(qint32(raw)) : double(raw);
        if (n > quint64(MaxSamples - out.size())) throw std::runtime_error("sample table exceeds resource limit");
        for (quint64 j = 0; j < n; ++j) out.append(value);
    }
    return out;
}
struct HeaderScan { qint64 moovOffset = -1; quint64 moovLength = 0; quint64 skip = 0; };
HeaderScan scanHeaders(const QByteArray& b)
{
    HeaderScan result;
    quint64 p = 0;
    while (p + 8 <= quint64(b.size())) {
        auto size = uintAt(b, p, 4);
        int header = 8;
        if (size == 1) { if (p + 16 > quint64(b.size())) break; size = uintAt(b, p + 8, 8); header = 16; }
        if (size < quint64(header)) throw std::runtime_error("Invalid box length");
        if (size > quint64(std::numeric_limits<qint64>::max()) - p) throw std::runtime_error("Invalid box length");
        if (b.mid(p + 4, 4) == "moov") { result.moovOffset = qint64(p); result.moovLength = size; return result; }
        p += size;
    }
    result.skip = p;
    return result;
}
}
SamplesResult inspectMp4Moov(const QByteArray& bytes, const QVector<int>& indexes)
{
    SamplesResult result;
    try {
        const auto outer = inspect(bytes);
        if (outer.isEmpty()) throw std::runtime_error("moov atom missing");
        const auto moov = inspect(outer.first().data);
        const auto mvhd = find(moov, "mvhd");
        const double movieScale = uintAt(mvhd.data, uintAt(mvhd.data, 0, 1) == 1 ? 20 : 12, 4);
        int index = -1;
        for (const auto& trak : moov) {
            if (trak.type != "trak") continue;
            ++index;
            const auto track = inspect(trak.data);
            const auto mdia = inspect(find(track, "mdia").data);
            const auto handler = find(mdia, "hdlr").data.mid(8, 4);
            if ((handler != "vide" && handler != "soun") || (!indexes.isEmpty() && !indexes.contains(index))) continue;
            const auto mdhd = find(mdia, "mdhd");
            const double timescale = uintAt(mdhd.data, uintAt(mdhd.data, 0, 1) == 1 ? 20 : 12, 4);
            const auto stbl = inspect(find(inspect(find(mdia, "minf").data), "stbl").data);
            const auto edts = find(track, "edts", false);
            const auto elst = find(inspect(edts.data), "elst", false);
            double start = 0;
            if (!elst.type.isEmpty()) {
                const auto version = uintAt(elst.data, 0, 1);
                const auto n = uintAt(elst.data, 4, 4);
                for (quint64 i = 0; i < n; ++i) {
                    const auto p = 8 + i * (version == 0 ? 12 : 20);
                    const double duration = double(uintAt(elst.data, p, version == 0 ? 4 : 8));
                    // Source v1 mediaTime is unsigned, even for -1 empty edits.
                    const double mediaTime = version == 0 ? double(qint32(uintAt(elst.data, p + 4, 4))) : double(uintAt(elst.data, p + 8, 8));
                    if (mediaTime >= 0) start -= mediaTime;
                    else if (mediaTime == -1) start += std::floor(duration * timescale / movieScale + 0.5);
                }
            }
            const auto durations = table(find(stbl, "stts", false), false);
            const auto offsets = table(find(stbl, "ctts", false), true);
            const auto stss = find(stbl, "stss", false);
            QSet<quint64> sync;
            if (!stss.type.isEmpty()) for (quint64 i = 0, n = uintAt(stss.data, 4, 4); i < n; ++i) sync.insert(uintAt(stss.data, 8 + i * 4, 4));
            QJsonArray samples;
            double dts = 0, total = 0;
            for (qsizetype i = 0; i < durations.size(); ++i) {
                // The source shifts sample zero; later DTS derives from that shifted sample.
                if (i == 0) dts = start;
                else dts += durations[i - 1];
                samples.append(QJsonObject{{"key", stss.type.isEmpty() || sync.contains(i + 1)},
                    {"pts", dts + (i < offsets.size() ? offsets[i] : 0)}, {"dts", dts}, {"duration", durations[i]}});
                total += durations[i];
            }
            result.tracks[QString::number(index)] = QJsonObject{{"type", handler == "vide" ? "video" : "audio"},
                {"timescale", timescale}, {"sampleDuration", durations.isEmpty() ? QJsonValue() : QJsonValue(total / durations.size())}, {"samples", samples}};
        }
        result.available = true;
    } catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); result.tracks = {}; }
    return result;
}
SamplesResult inspectMp4Samples(const QByteArray& bytes, const QVector<int>& indexes)
{
    SamplesResult result;
    quint64 offset = 0;
    try {
        for (int seek = 0; seek < 4; ++seek) {
            result.sourceOffsets.append(offset);
            const auto chunk = offset < quint64(bytes.size()) ? bytes.mid(offset, 65536) : QByteArray{};
            result.bytesFetched += chunk.size();
            const auto scan = scanHeaders(chunk);
            if (scan.moovOffset >= 0) {
                const auto start = offset + quint64(scan.moovOffset);
                auto moov = bytes.mid(start, qMin(scan.moovLength, quint64(bytes.size()) - start));
                result.bytesFetched += qMax<qint64>(0, moov.size() - (chunk.size() - scan.moovOffset));
                auto parsed = inspectMp4Moov(moov, indexes);
                parsed.bytesFetched = result.bytesFetched; parsed.sourceOffsets = result.sourceOffsets;
                return parsed;
            }
            if (scan.skip > quint64(std::numeric_limits<qint64>::max()) - offset) throw std::runtime_error("Invalid box length");
            offset += scan.skip;
        }
        result.error = "max seeks exceeded";
    } catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); }
    return result;
}

namespace {
// One QObject owns the stream, network manager and completion. No closures retain
// themselves; cancellation destroys all pending timers/replies without completion.
class Mp4Job final : public QObject {
public:
    QUrl url;
    QVector<int> indexes;
    std::function<void(SamplesResult)> callback;
    SamplesResult stats;
    QNetworkAccessManager manager{this};
    QPointer<QIODevice> stream;
    QByteArray chunk;
    quint64 offset = 0, moovLength = 0;
    int seeks = 0, redirects = 0;
    bool first = true, completed = false;
    explicit Mp4Job(QObject* parent) : QObject(parent) {}
    void finish(SamplesResult result) {
        if (completed) return;
        completed = true;
        result.bytesFetched = stats.bytesFetched;
        result.sourceOffsets = stats.sourceOffsets;
        QPointer<Mp4Job> guard(this);
        if (stream) { stream->disconnect(this); if (auto* r = qobject_cast<QNetworkReply*>(stream.data())) r->abort(); }
        auto done = std::move(callback);
        done(std::move(result));
        if (guard) deleteLater();
    }
    void fail(const QString& error) { SamplesResult result; result.error = error; finish(result); }
    void open() {
        if (completed) return;
        if (++seeks > 4) { fail("max seeks exceeded"); return; }
        stats.sourceOffsets.append(offset); first = true; chunk.clear();
        if (stream) { stream->disconnect(this); if (auto* r = qobject_cast<QNetworkReply*>(stream.data())) r->abort(); stream->deleteLater(); }
        if (url.isLocalFile()) {
            auto* file = new QFile(url.toLocalFile(), this); stream = file;
            if (!file->open(QIODevice::ReadOnly) || !file->seek(qint64(offset))) { fail(file->errorString()); return; }
            QTimer::singleShot(0, this, [this] { consume(true); });
        } else if (url.scheme() == "http" || url.scheme() == "https") {
            QNetworkRequest request(url);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            request.setRawHeader("range", "bytes=" + QByteArray::number(offset) + "-");
            auto* reply = manager.get(request); stream = reply;
            reply->setReadBufferSize(65536);
            connect(reply, &QNetworkReply::readyRead, this, [this] { consume(false); });
            connect(reply, &QNetworkReply::finished, this, [this, reply] {
                if (completed || stream != reply) return;
                const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (!target.isEmpty()) {
                    if (++redirects > 21) { fail("Maximum number of redirects exceeded"); return; }
                    url = reply->url().resolved(target); --seeks; stats.sourceOffsets.removeLast(); open(); return;
                }
                if (reply->error() != QNetworkReply::NoError && !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) { fail(reply->errorString()); return; }
                consume(true);
            });
        } else fail("Unsupported protocol");
    }
    void consume(bool ended) {
        if (completed || !stream) return;
        if (auto* reply = qobject_cast<QNetworkReply*>(stream.data()))
            if (!reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().isEmpty()) return;
        try {
            if (first) {
                auto data = stream->read(65536); stats.bytesFetched += data.size();
                if (data.isEmpty() && !ended) return;
                const auto scan = scanHeaders(data);
                if (scan.moovOffset < 0) {
                    if (scan.skip > quint64(std::numeric_limits<qint64>::max()) - offset) { fail("Invalid box length"); return; }
                    offset += scan.skip; open(); return;
                }
                first = false; moovLength = scan.moovLength; chunk = data.mid(scan.moovOffset);
                if (moovLength > 256 * 1024 * 1024) { fail("moov exceeds resource limit"); return; }
            }
            if (quint64(chunk.size()) < moovLength) {
                const auto data = stream->read(moovLength - chunk.size());
                stats.bytesFetched += data.size(); chunk += data;
            }
            if (quint64(chunk.size()) >= moovLength || ended) {
                finish(inspectMp4Moov(chunk.left(moovLength), indexes));
            }
        } catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); }
    }
};
}
QObject* retrieveMp4Samples(const QUrl& url, const QVector<int>& indexes, std::function<void(SamplesResult)> callback, QObject* parent)
{
    auto* job = new Mp4Job(parent); job->url = url; job->indexes = indexes; job->callback = std::move(callback);
    QTimer::singleShot(0, job, [job] { job->open(); });
    return job;
}
}
