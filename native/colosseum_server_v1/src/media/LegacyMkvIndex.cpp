#include "server1/media/LegacyMediaIndex.h"
#include <QJsonArray>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace server1::media {
namespace {
struct Element { quint64 id = 0, start = 0, content = 0, end = 0; QByteArray data; };
quint64 be(const QByteArray& b, quint64 p, int n)
{
    if (n < 1 || n > 8 || p > quint64(b.size()) || quint64(n) > quint64(b.size()) - p)
        throw std::runtime_error("truncated EBML field");
    quint64 result = 0;
    for (int i = 0; i < n; ++i) result = (result << 8) | quint8(b[p + i]);
    return result;
}
QPair<quint64, int> vint(const QByteArray& b, quint64 p, bool id)
{
    const auto first = be(b, p, 1);
    int length = 1;
    while (length <= 8 && !(first & (0x100 >> length))) ++length;
    if (length > 8 || (id && length > 4)) throw std::runtime_error("invalid EBML vint");
    const auto value = be(b, p, length);
    if (!id && length == 8 && (value & 0x00ffffffffffffffULL) >= (quint64(1) << 48))
        throw std::runtime_error("INVALID VINT value too big (length>7 bs=1)");
    return {id ? value : value & ((quint64(1) << (7 * length)) - 1), length};
}
Element element(const QByteArray& bytes, quint64 p, quint64 limit)
{
    const auto id = vint(bytes, p, true), size = vint(bytes, p + id.second, false);
    const auto content = p + id.second + size.second;
    const bool unknown = size.first == ((quint64(1) << (7 * size.second)) - 1);
    if (!unknown && size.first > std::numeric_limits<quint64>::max() - content)
        throw std::runtime_error("invalid EBML size");
    const auto end = unknown ? limit : qMin(limit, content + size.first);
    if (content > end) throw std::runtime_error("truncated EBML element");
    return {id.first, p, content, end, bytes.mid(content, end - content)};
}
QVector<Element> parseChildren(const QByteArray& bytes)
{
    QVector<Element> out;
    for (quint64 p = 0; p < quint64(bytes.size());) {
        try {
            auto e = element(bytes, p, bytes.size());
            out.append(e); p = e.end;
        } catch (const std::exception& e) {
            if (QByteArray(e.what()).startsWith("truncated EBML")) break;
            throw;
        }
    }
    return out;
}
Element find(const QVector<Element>& entries, quint64 id)
{
    for (const auto& e : entries) if (e.id == id) return e;
    return {};
}
double value(const Element& element)
{
    return element.id ? double(be(element.data, 0, element.data.size())) : std::numeric_limits<double>::quiet_NaN();
}
constexpr quint64 Info = 0x1549a966, Tracks = 0x1654ae6b, Cues = 0x1c53bb6b;
bool target(quint64 id) { return id == Info || id == Tracks || id == Cues; }
SamplesResult decodeSelected(const QVector<Element>& selected, const QVector<int>& indexes)
{
    SamplesResult result;
    try {
        const auto info = find(selected, Info), tracks = find(selected, Tracks), cues = find(selected, Cues);
        if (!info.id) throw std::runtime_error("Info element missing");
        if (!tracks.id) throw std::runtime_error("Tracks element missing");
        const double scale = value(find(parseChildren(info.data), 0x2ad7b1));
        if (cues.id && cues.data.isEmpty()) throw std::runtime_error("Cannot read properties of undefined (reading 'filter')");
        const auto points = parseChildren(cues.data);
        int index = -1;
        for (const auto& track : parseChildren(tracks.data)) {
            if (track.id != 0xae) continue;
            ++index;
            const auto fields = parseChildren(track.data);
            const double number = value(find(fields, 0xd7)), type = value(find(fields, 0x83));
            if ((type != 1 && type != 2) || (!indexes.isEmpty() && !indexes.contains(index))) continue;
            const auto defaultDuration = find(fields, 0x23e383);
            QVector<double> times;
            for (const auto& point : points) {
                if (point.id != 0xbb) continue;
                const auto values = parseChildren(point.data);
                const auto trackPositions = find(values, 0xb7);
                if (!trackPositions.id) throw std::runtime_error("Cannot read properties of undefined (reading 'children')");
                const auto cueTrack = find(parseChildren(trackPositions.data), 0xf7);
                if (!cueTrack.id) throw std::runtime_error("Cannot read properties of undefined (reading 'getUInt')");
                if (value(cueTrack) == number) {
                    const auto time = find(values, 0xb3);
                    if (!time.id) throw std::runtime_error("Cannot read properties of undefined (reading 'getUInt')");
                    times.append(value(time));
                }
            }
            // Legacy module 678 preserves CuePoint order; unlike v2, do not sort.
            QJsonArray samples;
            for (double time : times) samples.append(QJsonObject{{"key", true}, {"pts", time}});
            result.tracks[QString::number(index)] = QJsonObject{{"type", type == 1 ? "video" : "audio"},
                {"timescale", scale / 1000}, {"sampleDuration", defaultDuration.id ? QJsonValue(value(defaultDuration) / scale) : QJsonValue()}, {"samples", samples}};
        }
        result.available = true;
    } catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); result.tracks = {}; }
    return result;
}

}
SamplesResult inspectLegacyMkvSamples(const QByteArray& bytes, const QVector<int>& indexes)
{
    SamplesResult result;
    try {
        Element segment;
        for (quint64 p = 0; p < quint64(bytes.size());) {
            auto e = element(bytes, p, bytes.size());
            if (e.id == 0x18538067) { segment = e; break; }
            p = e.end;
        }
        if (!segment.id) throw std::runtime_error("Segment element missing");
        // Decoder.parseEbmlIDs follows sorted SeekHead positions and stops when
        // exhausted. Without SeekHead it stops at the first requested element.
        // Do not substitute a generic scan that silently discovers extra tracks.
        QVector<Element> selected;
        QVector<quint64> positions;
        quint64 p = segment.content;
        int traversals = 0;
        while (p < segment.end) {
            if (++traversals > 1000000) throw std::runtime_error("EBML traversal exceeds resource limit");
            auto e = element(bytes, p, segment.end);
            bool jump = false;
            if (e.id == 0x114d9b74) {
                for (auto seek : parseChildren(e.data)) {
                    if (seek.id != 0x4dbb) continue;
                    const auto fields = parseChildren(seek.data);
                    const auto id = find(fields, 0x53ab), offset = find(fields, 0x53ac);
                    if (id.id && offset.id && target(quint64(value(id)))) {
                        const auto raw = be(offset.data, 0, offset.data.size());
                        if (raw > std::numeric_limits<quint64>::max() - segment.content) throw std::runtime_error("invalid EBML seek offset");
                        positions.append(segment.content + raw);
                    }
                }
                std::sort(positions.begin(), positions.end());
                jump = true;
            } else if (target(e.id)) { selected.append(e); jump = true; }
            if (jump) {
                if (positions.isEmpty()) break;
                p = positions.takeFirst();
            } else p = e.end;
        }
        return decodeSelected(selected, indexes);
    } catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); result.tracks = {}; }
    return result;
}
namespace {
class MkvJob final : public QObject {
public:
    QUrl url;
    QVector<int> indexes;
    std::function<void(SamplesResult)> callback;
    SamplesResult stats;
    QNetworkAccessManager manager{this};
    QPointer<QIODevice> stream;
    QByteArray buffer;
    quint64 base = 0, cursor = 0, segmentContent = 0;
    quint64 segmentEnd = std::numeric_limits<quint64>::max();
    QVector<Element> selected;
    QVector<quint64> positions;
    bool segmentSeen = false, completed = false, ended = false;
    int redirects = 0, traversals = 0;
    explicit MkvJob(QObject* parent) : QObject(parent) {}
    void finish(SamplesResult result) {
        if (completed) return;
        completed = true; result.bytesFetched = stats.bytesFetched; result.sourceOffsets = stats.sourceOffsets;
        if (stream) { stream->disconnect(this); if (auto* r = qobject_cast<QNetworkReply*>(stream.data())) r->abort(); }
        auto done = std::move(callback); QPointer<MkvJob> guard(this);
        done(std::move(result)); if (guard) deleteLater();
    }
    void fail(const QString& error) { SamplesResult result; result.error = error; finish(result); }
    void open(quint64 offset) {
        if (completed) return;
        if (offset > quint64(std::numeric_limits<qint64>::max())) { fail("invalid EBML seek offset"); return; }
        if (stream) { stream->disconnect(this); if (auto* r = qobject_cast<QNetworkReply*>(stream.data())) r->abort(); stream->deleteLater(); }
        base = offset; buffer.clear(); ended = false; stats.sourceOffsets.append(offset);
        if (url.isLocalFile()) {
            auto* file = new QFile(url.toLocalFile(), this); stream = file;
            if (!file->open(QIODevice::ReadOnly) || !file->seek(qint64(offset))) { fail(file->errorString()); return; }
            QTimer::singleShot(0, this, [this] { read(); });
        } else if (url.scheme() == "http" || url.scheme() == "https") {
            QNetworkRequest request(url);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            if (offset) request.setRawHeader("range", "bytes=" + QByteArray::number(offset) + "-");
            auto* reply = manager.get(request); stream = reply; reply->setReadBufferSize(65536);
            connect(reply, &QNetworkReply::readyRead, this, [this] { read(); });
            connect(reply, &QNetworkReply::finished, this, [this, reply, offset] {
                if (completed || stream != reply) return;
                const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (!target.isEmpty()) {
                    if (++redirects > 21) { fail("Maximum number of redirects exceeded"); return; }
                    url = reply->url().resolved(target); stats.sourceOffsets.removeLast(); open(offset); return;
                }
                if (reply->error() != QNetworkReply::NoError && !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) { fail(reply->errorString()); return; }
                ended = true; read();
            });
        } else fail("Unsupported protocol");
    }
    void read() {
        if (completed || !stream) return;
        if (auto* r = qobject_cast<QNetworkReply*>(stream.data()))
            if (!r->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().isEmpty()) return;
        const auto bytes = stream->read(65536);
        stats.bytesFetched += bytes.size(); buffer += bytes;
        if (qobject_cast<QFile*>(stream.data()) && stream->atEnd()) ended = true;
        process();
    }
    bool need(quint64 length) {
        if (cursor < base || cursor > base + quint64(buffer.size())) {
            // The source decoder reopens for skips of at least 32000 bytes.
            const auto end = base + quint64(buffer.size());
            if (cursor < base || cursor - end >= 32000) { open(cursor); return false; }
        }
        if (cursor >= base && length <= quint64(buffer.size()) && cursor - base <= quint64(buffer.size()) - length) return true;
        if (ended) {
            if (segmentSeen) finish(decodeSelected(selected, indexes));
            else fail("Segment element missing");
            return false;
        }
        if (qobject_cast<QFile*>(stream.data()) || stream->bytesAvailable() > 0)
            QTimer::singleShot(0, this, [this] { read(); });
        return false;
    }
    void process() {
        try {
            while (!completed) {
                if (segmentSeen && cursor >= segmentEnd) { finish(decodeSelected(selected, indexes)); return; }
                if (!need(2)) return;
                const auto local = cursor - base;
                // Read enough for the variable-width element ID and size.
                int idLength = 1;
                while (idLength <= 4 && !(quint8(buffer[local]) & (0x100 >> idLength))) ++idLength;
                if (idLength > 4) { fail("invalid EBML vint"); return; }
                if (!need(idLength + 1)) return;
                int sizeLength = 1;
                while (sizeLength <= 8 && !(quint8(buffer[local + idLength]) & (0x100 >> sizeLength))) ++sizeLength;
                if (sizeLength > 8) { fail("invalid EBML vint"); return; }
                if (!need(idLength + sizeLength)) return;
                const auto id = be(buffer, local, idLength);
                const auto size = vint(buffer, local + idLength, false).first;
                const quint64 header = idLength + sizeLength;
                const bool unknown = size == ((quint64(1) << (7 * sizeLength)) - 1);
                if (size > std::numeric_limits<quint64>::max() - cursor - header) { fail("invalid EBML size"); return; }
                if (!segmentSeen && id == 0x18538067) {
                    segmentSeen = true; segmentContent = cursor + header;
                    segmentEnd = unknown ? std::numeric_limits<quint64>::max() : segmentContent + size;
                    cursor = segmentContent; continue;
                }
                if (!segmentSeen) { cursor += header + size; continue; }
                if (target(id) || id == 0x114d9b74) {
                    if (unknown || size > 256 * 1024 * 1024) { fail("EBML metadata exceeds resource limit"); return; }
                    if (ended && cursor >= base && header + size > quint64(buffer.size()) - (cursor - base)) {
                        if (target(id)) selected.append({id, cursor, cursor + header, base + quint64(buffer.size()), buffer.mid(local + header)});
                        finish(decodeSelected(selected, indexes)); return;
                    }
                    if (!need(header + size)) return;
                    if (++traversals > 1000000) { fail("EBML traversal exceeds resource limit"); return; }
                    Element e{id, cursor, cursor + header, cursor + header + size, buffer.mid(local + header, size)};
                    if (id == 0x114d9b74) {
                        for (const auto& seek : parseChildren(e.data)) {
                            if (seek.id != 0x4dbb) continue;
                            const auto fields = parseChildren(seek.data);
                            const auto seekId = find(fields, 0x53ab), offset = find(fields, 0x53ac);
                            if (seekId.id && offset.id && target(quint64(value(seekId)))) {
                                const auto raw = be(offset.data, 0, offset.data.size());
                                if (raw > std::numeric_limits<quint64>::max() - segmentContent) { fail("invalid EBML seek offset"); return; }
                                positions.append(segmentContent + raw);
                            }
                        }
                        std::sort(positions.begin(), positions.end());
                    } else selected.append(e);
                    if (positions.isEmpty()) { finish(decodeSelected(selected, indexes)); return; }
                    cursor = positions.takeFirst();
                } else {
                    if (unknown) { fail("unsupported unknown-length EBML element"); return; }
                    cursor += header + size;
                }
                // Retain the current chunk, permitting the source's in-buffer seeks.
                // Compact only after a sequential read has consumed the full buffer.
                if (cursor == base + quint64(buffer.size())) { base = cursor; buffer.clear(); }
            }
        } catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); }
    }
};
}
QObject* retrieveLegacyMkvSamples(const QUrl& url, const QVector<int>& indexes, std::function<void(SamplesResult)> callback, QObject* parent)
{
    auto* job = new MkvJob(parent); job->url = url; job->indexes = indexes; job->callback = std::move(callback);
    QTimer::singleShot(0, job, [job] { job->open(0); });
    return job;
}
}
