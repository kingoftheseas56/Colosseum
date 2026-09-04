#include "RemoteArchive.h"
#include "LzString.h"
#include "miniz.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace Colosseum::Server::RemoteArchive {
namespace {

constexpr auto kDlnaTransferMode = "Streaming";
constexpr auto kDlnaFeatures = "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000";

QByteArray lowerName(QByteArray name)
{
    return name.trimmed().toLower();
}

QByteArray lookupHeader(const QHash<QByteArray, QByteArray> &headers, const QByteArray &name)
{
    const QByteArray wanted = lowerName(name);
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (lowerName(it.key()) == wanted) return it.value();
    }
    return {};
}

void setHeaderCaseInsensitive(QHash<QByteArray, QByteArray> &headers,
                              const QByteArray &name, const QByteArray &value)
{
    const QByteArray wanted = lowerName(name);
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (lowerName(it.key()) == wanted) {
            it.value() = value;
            return;
        }
    }
    headers.insert(name, value);
}

quint16 readLe16(const QByteArray &bytes, qsizetype off)
{
    if (off < 0 || off + 2 > bytes.size()) return 0;
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData() + off);
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 readLe32(const QByteArray &bytes, qsizetype off)
{
    if (off < 0 || off + 4 > bytes.size()) return 0;
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData() + off);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

qint64 parseOctal(QByteArray field)
{
    const qsizetype nul = field.indexOf('\0');
    if (nul >= 0) field.truncate(nul);
    field = field.trimmed();
    bool ok = false;
    const qint64 value = field.toLongLong(&ok, 8);
    return ok ? value : -1;
}

QString fallbackName(const QUrl &url, ArchiveKind kind)
{
    QString name = QFileInfo(url.path()).fileName();
    if (!name.isEmpty() && name.contains('.')) return QUrl::fromPercentEncoding(name.toUtf8());
    switch (kind) {
    case ArchiveKind::Rar: return QStringLiteral("archive.rar");
    case ArchiveKind::Zip: return QStringLiteral("archive.zip");
    case ArchiveKind::SevenZip: return QStringLiteral("archive.7z");
    case ArchiveKind::Tar: return QStringLiteral("archive.tar");
    case ArchiveKind::Tgz: return QStringLiteral("archive.gz");
    }
    return QStringLiteral("archive.bin");
}

QString storeKey(ArchiveKind kind, const QString &key)
{
    return archiveKindName(kind) + QLatin1Char(':') + key;
}

QVector<SourceSpec> parseSourceArray(const QJsonArray &array)
{
    QVector<SourceSpec> sources;
    for (const QJsonValue &value : array) {
        SourceSpec spec;
        if (value.isString()) {
            spec.url = QUrl(value.toString());
        } else if (value.isArray()) {
            const QJsonArray tuple = value.toArray();
            if (!tuple.isEmpty() && tuple[0].isString()) spec.url = QUrl(tuple[0].toString());
            if (tuple.size() > 1 && tuple[1].isDouble()) spec.bytes = static_cast<qint64>(tuple[1].toDouble());
        } else if (value.isObject()) {
            const QJsonObject object = value.toObject();
            spec.url = QUrl(object.value(QStringLiteral("url")).toString());
            if (object.value(QStringLiteral("bytes")).isDouble()) spec.bytes = static_cast<qint64>(object.value(QStringLiteral("bytes")).toDouble());
        }
        if (spec.url.isValid() && !spec.url.isEmpty()) sources.push_back(spec);
    }
    return sources;
}

QString sha256Hex(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool matchesOptions(const QString &name, int fileIndex, const ArchiveOptions &options)
{
    if (!options.fileMustInclude.isEmpty()) {
        for (const QString &pattern : options.fileMustInclude) {
            QRegularExpression expression(pattern, QRegularExpression::CaseInsensitiveOption);
            if (!expression.isValid()) continue;
            if (expression.match(name).hasMatch()) return true;
        }
        return false;
    }
    if (options.fileIndex.has_value()) return fileIndex == *options.fileIndex;
    static const QRegularExpression video(QStringLiteral(R"((\.mkv$|\.mp4$|\.avi$|\.ts$))"),
                                          QRegularExpression::CaseInsensitiveOption);
    return video.match(name).hasMatch();
}

struct ParsedRange {
    bool present = false;
    bool valid = true;
    qint64 start = 0;
    qint64 end = -1;
};

ParsedRange parseHttpRange(const QByteArray &raw, qint64 size)
{
    ParsedRange out;
    if (raw.isEmpty()) return out;
    out.present = true;
    QByteArray value = raw.trimmed();
    if (!value.startsWith("bytes=")) {
        out.valid = false;
        return out;
    }
    value.remove(0, 6);
    if (value.contains(',')) {
        out.valid = false;
        return out;
    }
    const int dash = value.indexOf('-');
    if (dash < 0) {
        out.valid = false;
        return out;
    }
    const QByteArray left = value.left(dash).trimmed();
    const QByteArray right = value.mid(dash + 1).trimmed();
    bool leftOk = false, rightOk = false;
    if (!left.isEmpty()) out.start = left.toLongLong(&leftOk);
    if (!right.isEmpty()) out.end = right.toLongLong(&rightOk);
    if (left.isEmpty() && right.isEmpty()) {
        out.valid = false;
        return out;
    }
    if (left.isEmpty()) {
        if (!rightOk || out.end <= 0) {
            out.valid = false;
            return out;
        }
        const qint64 suffix = out.end;
        out.start = std::max<qint64>(0, size - suffix);
        out.end = size - 1;
    } else {
        if (!leftOk || out.start < 0) {
            out.valid = false;
            return out;
        }
        if (right.isEmpty()) out.end = size - 1;
        else if (!rightOk) out.valid = false;
    }
    if (out.start >= size || out.end >= size || out.end < out.start || out.end < 0) out.valid = false;
    return out;
}

QByteArray readAll(RemoteRangeSource &source, RemoteError *error, CancellationToken *cancel = nullptr)
{
    RemoteError lengthError;
    const qint64 size = source.length(&lengthError, cancel);
    if (!lengthError.ok() || size < 0) {
        if (error) *error = lengthError.ok()
            ? RemoteError{RemoteErrorCode::Transport, QStringLiteral("remote length unavailable"), 0}
            : lengthError;
        return {};
    }
    if (size == 0) {
        if (error) *error = {};
        return {};
    }
    RemoteRead read = source.read(0, size - 1, cancel);
    if (error) *error = read.error;
    return read.bytes;
}

bool materializeSource(RemoteRangeSource &source, QFile &file,
                       RemoteError *error, CancellationToken *cancel)
{
    RemoteError lengthError;
    const qint64 size = source.length(&lengthError, cancel);
    if (!lengthError.ok() || size < 0) {
        if (error) *error = lengthError.ok()
            ? RemoteError{RemoteErrorCode::Transport, QStringLiteral("remote length unavailable"), 0}
            : lengthError;
        return false;
    }

    constexpr qint64 chunkSize = 1024 * 1024;
    for (qint64 offset = 0; offset < size; offset += chunkSize) {
        if (cancel && cancel->isCancelled()) {
            if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
            return false;
        }
        const qint64 end = std::min(size - 1, offset + chunkSize - 1);
        const RemoteRead read = source.read(offset, end, cancel);
        if (!read.error.ok()) {
            if (error) *error = read.error;
            return false;
        }
        const qint64 expected = end - offset + 1;
        if (read.bytes.size() != expected) {
            if (error) *error = {RemoteErrorCode::Protocol,
                                  QStringLiteral("remote source returned a short archive volume range"), 0};
            return false;
        }
        qint64 written = 0;
        while (written < read.bytes.size()) {
            const qint64 count = file.write(read.bytes.constData() + written,
                                            read.bytes.size() - written);
            if (count <= 0) {
                if (error) *error = {RemoteErrorCode::Transport,
                                      QStringLiteral("cannot materialize archive volume"), 0};
                return false;
            }
            written += count;
        }
    }
    if (error) *error = {};
    return true;
}

QByteArray inflateRaw(const QByteArray &compressed, qint64 expectedSize, RemoteError *error)
{
    size_t outSize = 0;
    void *out = tinfl_decompress_mem_to_heap(compressed.constData(), static_cast<size_t>(compressed.size()),
                                             &outSize, 0);
    if (!out) {
        if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("deflate stream could not be decoded"), 0};
        return {};
    }
    QByteArray bytes(reinterpret_cast<const char *>(out), static_cast<qsizetype>(outSize));
    mz_free(out);
    if (expectedSize >= 0 && bytes.size() != expectedSize) {
        if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("deflate size mismatch"), 0};
        return {};
    }
    if (error) *error = {};
    return bytes;
}

QByteArray gunzip(const QByteArray &gzip, RemoteError *error)
{
    if (gzip.size() < 18 || quint8(gzip[0]) != 0x1f || quint8(gzip[1]) != 0x8b || quint8(gzip[2]) != 8) {
        if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("invalid gzip header"), 0};
        return {};
    }
    qsizetype pos = 10;
    const quint8 flags = quint8(gzip[3]);
    if (flags & 0x04) {
        if (pos + 2 > gzip.size()) goto invalid;
        const quint16 extra = readLe16(gzip, pos); pos += 2 + extra;
    }
    if (flags & 0x08) {
        while (pos < gzip.size() && gzip[pos] != '\0') ++pos;
        ++pos;
    }
    if (flags & 0x10) {
        while (pos < gzip.size() && gzip[pos] != '\0') ++pos;
        ++pos;
    }
    if (flags & 0x02) pos += 2;
    if (pos < 0 || pos > gzip.size() - 8) goto invalid;
    {
        const QByteArray deflated = gzip.mid(pos, gzip.size() - pos - 8);
        QByteArray out = inflateRaw(deflated, -1, error);
        if (error && !error->ok()) return {};
        const quint32 expectedSize = readLe32(gzip, gzip.size() - 4);
        if (quint32(out.size()) != expectedSize) {
            if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("gzip size mismatch"), 0};
            return {};
        }
        const quint32 expectedCrc = readLe32(gzip, gzip.size() - 8);
        const quint32 actualCrc = static_cast<quint32>(mz_crc32(MZ_CRC32_INIT,
            reinterpret_cast<const unsigned char *>(out.constData()), out.size()));
        if (actualCrc != expectedCrc) {
            if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("gzip CRC mismatch"), 0};
            return {};
        }
        if (error) *error = {};
        return out;
    }
invalid:
    if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("truncated gzip header"), 0};
    return {};
}

QString archiveToolPath()
{
#ifdef Q_OS_WIN
    const QString systemTar = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(systemTar)) return systemTar;
#endif
    QString path = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (path.isEmpty()) path = QStandardPaths::findExecutable(QStringLiteral("tar"));
    return path;
}

QByteArray processOutput(const QString &program, const QStringList &arguments,
                         RemoteError *error, CancellationToken *cancel = nullptr)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments, QIODevice::ReadOnly);
    for (int waited = 0; !process.waitForStarted(100); waited += 100) {
        if (cancel && cancel->isCancelled()) {
            process.kill();
            process.waitForFinished(2000);
            if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
            return {};
        }
        if (waited >= 5000) {
            if (error) *error = {RemoteErrorCode::Unsupported, QStringLiteral("archive extractor could not start"), 0};
            return {};
        }
    }
    if (cancel && cancel->isCancelled()) {
        process.kill();
        process.waitForFinished(2000);
        if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
        return {};
    }
    for (int waited = 0; !process.waitForFinished(100); waited += 100) {
        if (cancel && cancel->isCancelled()) {
            process.kill();
            process.waitForFinished(2000);
            if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
            return {};
        }
        if (waited >= 60000) {
            process.kill();
            process.waitForFinished(2000);
            if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("archive extractor timed out"), 0};
            return {};
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = {RemoteErrorCode::Parser,
            QString::fromUtf8(process.readAllStandardError()).trimmed(), 0};
        return {};
    }
    if (error) *error = {};
    return process.readAllStandardOutput();
}

} // namespace

MemoryRangeSource::MemoryRangeSource(QString name, QByteArray bytes)
    : m_name(std::move(name)), m_bytes(std::move(bytes)) {}

qint64 MemoryRangeSource::length(RemoteError *error, CancellationToken *cancel)
{
    if (cancel && cancel->isCancelled()) {
        if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
        return -1;
    }
    if (error) *error = {};
    return m_bytes.size();
}

RemoteRead MemoryRangeSource::read(qint64 start, std::optional<qint64> endInclusive,
                                   CancellationToken *cancel)
{
    RemoteRead out; out.totalLength = m_bytes.size(); out.start = start;
    if (cancel && cancel->isCancelled()) {
        out.error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
        return out;
    }
    const qint64 end = endInclusive.value_or(m_bytes.size() - 1);
    if (start < 0 || start >= m_bytes.size() || end < start || end >= m_bytes.size()) {
        out.error = {RemoteErrorCode::RangeNotSatisfiable, QStringLiteral("range outside source"), 416};
        return out;
    }
    out.end = end;
    out.bytes = m_bytes.mid(start, end - start + 1);
    return out;
}

struct HttpRangeSource::ReplyData {
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
    RemoteError error;
};

HttpRangeSource::HttpRangeSource(QNetworkAccessManager *network, QUrl url, QString displayName)
    : m_network(network), m_url(std::move(url)), m_displayName(std::move(displayName)) {}

QString HttpRangeSource::name() const
{
    return m_displayName.isEmpty() ? QFileInfo(m_url.path()).fileName() : m_displayName;
}

HttpRangeSource::ReplyData HttpRangeSource::perform(const QByteArray &method, const QByteArray &range,
                                                    CancellationToken *cancel, int maxAttempts)
{
    ReplyData final;
    if (!m_network || !m_url.isValid()) {
        final.error = {RemoteErrorCode::InvalidRequest, QStringLiteral("invalid remote source"), 0};
        return final;
    }
    for (int attempt = 0; attempt < std::max(1, maxAttempts); ++attempt) {
        if (cancel && cancel->isCancelled()) {
            final.error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
            return final;
        }
        QNetworkRequest request(m_url);
        request.setTransferTimeout(30000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        if (!range.isEmpty()) request.setRawHeader("Range", range);
        QNetworkReply *reply = method == "HEAD" ? m_network->head(request) : m_network->get(request);
        QObject::connect(reply, &QNetworkReply::sslErrors, reply,
                         [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
        QEventLoop loop;
        QTimer cancelTimer;
        cancelTimer.setInterval(10);
        if (cancel) {
            QObject::connect(&cancelTimer, &QTimer::timeout, reply, [reply, cancel] {
                if (cancel->isCancelled()) reply->abort();
            });
            cancelTimer.start();
        }
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        cancelTimer.stop();

        ReplyData current;
        current.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        for (const auto &pair : reply->rawHeaderPairs()) current.headers.insert(pair.first, pair.second);
        current.body = reply->readAll();
        const auto networkError = reply->error();
        const QString networkMessage = reply->errorString();
        reply->deleteLater();

        if (cancel && cancel->isCancelled()) {
            current.error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), current.status};
            return current;
        }
        if (current.status == 416) {
            current.error = {RemoteErrorCode::RangeNotSatisfiable, QStringLiteral("remote range not satisfiable"), 416};
            return current;
        }
        const bool transientStatus = current.status == 408 || current.status == 429 ||
                                     (current.status >= 500 && current.status <= 504);
        if (networkError != QNetworkReply::NoError || current.status >= 400) {
            current.error = {RemoteErrorCode::Transport,
                             networkMessage.isEmpty() ? QStringLiteral("remote request failed") : networkMessage,
                             current.status};
            if (attempt + 1 < maxAttempts && transientStatus) {
                final = current;
                continue;
            }
            return current;
        }
        current.error = {};
        return current;
    }
    return final;
}

qint64 HttpRangeSource::length(RemoteError *error, CancellationToken *cancel)
{
    if (m_length >= 0) {
        if (error) *error = {};
        return m_length;
    }
    ReplyData head = perform("HEAD", {}, cancel, 2);
    if (head.error.ok()) {
        bool ok = false;
        const qint64 value = lookupHeader(head.headers, "Content-Length").toLongLong(&ok);
        if (ok && value >= 0) {
            m_length = value;
            if (error) *error = {};
            return m_length;
        }
    }
    ReplyData probe = perform("GET", "bytes=0-0", cancel, 2);
    if (!probe.error.ok()) {
        if (error) *error = probe.error;
        return -1;
    }
    const QByteArray contentRange = lookupHeader(probe.headers, "Content-Range");
    const int slash = contentRange.lastIndexOf('/');
    bool ok = false;
    const qint64 total = slash >= 0 ? contentRange.mid(slash + 1).toLongLong(&ok) : -1;
    if (!ok) {
        const qint64 fallback = lookupHeader(probe.headers, "Content-Length").toLongLong(&ok);
        if (ok && probe.status == 200) m_length = fallback;
    } else {
        m_length = total;
    }
    if (m_length < 0) {
        if (error) *error = {RemoteErrorCode::Protocol, QStringLiteral("remote length unavailable"), probe.status};
        return -1;
    }
    if (error) *error = {};
    return m_length;
}

RemoteRead HttpRangeSource::read(qint64 start, std::optional<qint64> endInclusive,
                                 CancellationToken *cancel)
{
    RemoteRead out; out.start = start;
    if (start < 0 || (endInclusive.has_value() && *endInclusive < start)) {
        out.error = {RemoteErrorCode::InvalidRequest, QStringLiteral("invalid byte range"), 0};
        return out;
    }
    QByteArray range = "bytes=" + QByteArray::number(start) + "-";
    if (endInclusive.has_value()) range += QByteArray::number(*endInclusive);
    ReplyData reply = perform("GET", range, cancel, 3);
    if (!reply.error.ok()) {
        out.error = reply.error;
        return out;
    }
    qint64 total = -1;
    const QByteArray contentRange = lookupHeader(reply.headers, "Content-Range");
    const int slash = contentRange.lastIndexOf('/');
    bool totalOk = false;
    if (slash >= 0) total = contentRange.mid(slash + 1).toLongLong(&totalOk);
    if (!totalOk && m_length >= 0) total = m_length;
    if (!totalOk && reply.status == 200) total = reply.body.size();
    if (total >= 0) m_length = total;
    out.totalLength = m_length;
    const qint64 wantedEnd = endInclusive.value_or(m_length >= 0 ? m_length - 1 : start + reply.body.size() - 1);
    if (reply.status == 200 && start > 0) {
        // Some endpoints ignore Range. Preserve the random-access contract by slicing
        // their full response rather than leaking wrong bytes to archive parsers.
        if (start >= reply.body.size()) {
            out.error = {RemoteErrorCode::RangeNotSatisfiable, QStringLiteral("range outside remote response"), 416};
            return out;
        }
        const qint64 count = endInclusive.has_value() ? wantedEnd - start + 1 : -1;
        out.bytes = reply.body.mid(start, count);
    } else {
        out.bytes = reply.body;
        if (endInclusive.has_value() && out.bytes.size() > wantedEnd - start + 1)
            out.bytes.truncate(wantedEnd - start + 1);
    }
    out.end = start + out.bytes.size() - 1;
    return out;
}

ConcatenatedRangeSource::ConcatenatedRangeSource(QString name,
                                                 QVector<std::shared_ptr<RemoteRangeSource>> parts)
    : m_name(std::move(name)), m_parts(std::move(parts)) {}

bool ConcatenatedRangeSource::ensureLengths(RemoteError *error, CancellationToken *cancel)
{
    if (m_total >= 0) {
        if (error) *error = {};
        return true;
    }
    m_lengths.clear(); m_total = 0;
    for (const auto &part : m_parts) {
        if (!part) {
            if (error) *error = {RemoteErrorCode::InvalidRequest, QStringLiteral("null source part"), 0};
            m_total = -1; return false;
        }
        RemoteError partError;
        const qint64 len = part->length(&partError, cancel);
        if (!partError.ok() || len < 0) {
            if (error) *error = partError;
            m_total = -1; return false;
        }
        m_lengths.push_back(len); m_total += len;
    }
    if (error) *error = {};
    return true;
}

qint64 ConcatenatedRangeSource::length(RemoteError *error, CancellationToken *cancel)
{
    return ensureLengths(error, cancel) ? m_total : -1;
}

RemoteRead ConcatenatedRangeSource::read(qint64 start, std::optional<qint64> endInclusive,
                                         CancellationToken *cancel)
{
    RemoteRead out; out.start = start;
    RemoteError error;
    if (!ensureLengths(&error, cancel)) { out.error = error; return out; }
    const qint64 end = endInclusive.value_or(m_total - 1);
    if (start < 0 || start >= m_total || end < start || end >= m_total) {
        out.error = {RemoteErrorCode::RangeNotSatisfiable, QStringLiteral("range outside merged source"), 416};
        return out;
    }
    qint64 logicalOffset = 0;
    for (int i = 0; i < m_parts.size(); ++i) {
        const qint64 partStart = logicalOffset;
        const qint64 partEnd = logicalOffset + m_lengths[i] - 1;
        logicalOffset += m_lengths[i];
        if (end < partStart) break;
        if (start > partEnd) continue;
        const qint64 localStart = std::max<qint64>(0, start - partStart);
        const qint64 localEnd = std::min<qint64>(m_lengths[i] - 1, end - partStart);
        RemoteRead chunk = m_parts[i]->read(localStart, localEnd, cancel);
        if (!chunk.error.ok()) { out.error = chunk.error; return out; }
        out.bytes += chunk.bytes;
    }
    out.totalLength = m_total; out.end = end;
    return out;
}

QByteArray Request::header(const QByteArray &name) const { return lookupHeader(headers, name); }
QByteArray Response::header(const QByteArray &name) const { return lookupHeader(headers, name); }
void Response::setHeader(const QByteArray &name, const QByteArray &value) { setHeaderCaseInsensitive(headers, name, value); }

QString archiveKindName(ArchiveKind kind)
{
    switch (kind) {
    case ArchiveKind::Rar: return QStringLiteral("rar");
    case ArchiveKind::Zip: return QStringLiteral("zip");
    case ArchiveKind::SevenZip: return QStringLiteral("7zip");
    case ArchiveKind::Tar: return QStringLiteral("tar");
    case ArchiveKind::Tgz: return QStringLiteral("tgz");
    }
    return QStringLiteral("archive");
}

QByteArray contentTypeForName(const QString &name)
{
    QMimeDatabase database;
    const QByteArray mime = database.mimeTypeForFile(name, QMimeDatabase::MatchExtension).name().toUtf8();
    return mime.isEmpty() ? QByteArray("application/octet-stream") : mime;
}

ArchiveOptions parseArchiveOptions(const QString &json, RemoteError *error)
{
    ArchiveOptions options;
    if (json.isEmpty()) { if (error) *error = {}; return options; }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = {RemoteErrorCode::InvalidRequest, QStringLiteral("invalid archive options"), 0};
        return options;
    }
    const QJsonObject object = doc.object();
    const QJsonArray include = object.value(QStringLiteral("fileMustInclude")).toArray();
    for (const QJsonValue &value : include) if (value.isString()) options.fileMustInclude.push_back(value.toString());
    if (object.contains(QStringLiteral("fileIdx")) && object.value(QStringLiteral("fileIdx")).isDouble())
        options.fileIndex = object.value(QStringLiteral("fileIdx")).toInt();
    if (object.contains(QStringLiteral("maxFiles")) && object.value(QStringLiteral("maxFiles")).isDouble())
        options.maxFiles = object.value(QStringLiteral("maxFiles")).toInt();
    if (error) *error = {};
    return options;
}

struct ArchiveService::SelectedEntry {
    QString name;
    qint64 size = -1;
    qint64 sourceOffset = 0;
    std::shared_ptr<RemoteRangeSource> source;
    QByteArray materialized;
    bool directRange = false;
    bool compressedZip = false;
    bool valid = false;
};

ArchiveService::ArchiveService(SourceFactory factory) : m_factory(std::move(factory))
{
    if (!m_factory) m_ownedNetwork = std::make_unique<QNetworkAccessManager>();
}

QVector<SourceSpec> ArchiveService::sourcesForTesting(ArchiveKind kind, const QString &key) const
{
    return m_store.value(storeKey(kind, key)).sources;
}

std::shared_ptr<RemoteRangeSource> ArchiveService::sourceFor(const SourceSpec &spec, RemoteError *error)
{
    if (m_factory) return m_factory(spec, error);
    if (error) *error = {};
    return std::make_shared<HttpRangeSource>(m_ownedNetwork.get(), spec.url, QFileInfo(spec.url.path()).fileName());
}

std::shared_ptr<RemoteRangeSource> ArchiveService::mergedSource(const StoredArchive &stored, RemoteError *error)
{
    QVector<std::shared_ptr<RemoteRangeSource>> parts;
    for (const SourceSpec &spec : stored.sources) {
        auto source = sourceFor(spec, error);
        if (!source || (error && !error->ok())) return {};
        if (spec.bytes >= 0) {
            // An authoritative byte count came from the upstream create payload. The
            // source still validates reads, but length discovery is avoided by the
            // wrapper to preserve module 1125/1281 behavior for costly remotes.
            class SizedSource final : public RemoteRangeSource {
            public:
                SizedSource(std::shared_ptr<RemoteRangeSource> inner, qint64 size)
                    : m_inner(std::move(inner)), m_size(size) {}
                qint64 length(RemoteError *e, CancellationToken *c) override {
                    if (c && c->isCancelled()) { if (e) *e = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0}; return -1; }
                    if (e) *e = {}; return m_size;
                }
                RemoteRead read(qint64 s, std::optional<qint64> end, CancellationToken *c) override { return m_inner->read(s, end, c); }
                QString name() const override { return m_inner->name(); }
            private:
                std::shared_ptr<RemoteRangeSource> m_inner; qint64 m_size;
            };
            source = std::make_shared<SizedSource>(source, spec.bytes);
        }
        parts.push_back(std::move(source));
    }
    if (parts.isEmpty()) {
        if (error) *error = {RemoteErrorCode::InvalidRequest, QStringLiteral("archive has no sources"), 0};
        return {};
    }
    if (parts.size() == 1) { if (error) *error = {}; return parts.first(); }
    if (error) *error = {};
    return std::make_shared<ConcatenatedRangeSource>(QStringLiteral("merged-%1").arg(archiveKindName(stored.kind)), parts);
}

QString ArchiveService::storeSources(ArchiveKind kind, QVector<SourceSpec> sources, const QString &requestedKey)
{
    auto ordinal = [kind](const SourceSpec &spec) -> qint64 {
        const QString path = spec.url.path().section('?', 0, 0);
        QRegularExpression re;
        qint64 defaultValue = 0;
        switch (kind) {
        case ArchiveKind::Zip:
            re = QRegularExpression(QStringLiteral(R"(\.Z(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption);
            defaultValue = 99999; break;
        case ArchiveKind::SevenZip:
            re = QRegularExpression(QStringLiteral(R"(\.7Z\.(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption);
            break;
        case ArchiveKind::Tar:
            re = QRegularExpression(QStringLiteral(R"(\.TAR\.(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption);
            break;
        case ArchiveKind::Tgz:
            re = QRegularExpression(QStringLiteral(R"(\.(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption);
            break;
        case ArchiveKind::Rar:
            return 0; // module 1232 is called with no ordering regex for /rar.
        }
        const auto match = re.match(path);
        return match.hasMatch() ? match.captured(1).toLongLong() : defaultValue;
    };
    if (kind != ArchiveKind::Rar) {
        std::stable_sort(sources.begin(), sources.end(), [&](const SourceSpec &a, const SourceSpec &b) {
            return ordinal(a) < ordinal(b);
        });
    }
    QString key = requestedKey;
    if (key.isEmpty()) key = QUuid::createUuid().toString(QUuid::Id128).left(12);
    const QString compound = storeKey(kind, key);
    if (!m_store.contains(compound)) m_store.insert(compound, StoredArchive{kind, std::move(sources)});
    return key;
}

Response ArchiveService::handle(ArchiveKind kind, const Request &request)
{
    if (request.path == QStringLiteral("/create") || request.path.startsWith(QStringLiteral("/create/")))
        return handleCreate(kind, request);
    if (request.path == QStringLiteral("/stream")) return handleStream(kind, request);
    Response notFound; notFound.status = 404; return notFound;
}

Response ArchiveService::handleCreate(ArchiveKind kind, const Request &request)
{
    Response response;
    if (request.method == "POST") {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            response.status = 500; response.setHeader("Content-Type", "text/plain");
            response.body = "Cannot parse JSON data, err 1"; return response;
        }
        QString requestedKey;
        if (request.path.startsWith(QStringLiteral("/create/"))) requestedKey = request.path.mid(QStringLiteral("/create/").size());
        const QString key = storeSources(kind, parseSourceArray(doc.array()), requestedKey);
        response.status = 200; response.setHeader("Content-Type", "application/json");
        response.body = QJsonDocument(QJsonObject{{QStringLiteral("key"), key}}).toJson(QJsonDocument::Compact);
        response.setHeader("Content-Length", QByteArray::number(response.body.size()));
        return response;
    }

    // Stremio modules 1232/1283/1291/1298/1305: ALL /create accepts an
    // lz-string encoded JSON bootstrap, stores it under SHA-256(lz), then redirects.
    const QString lz = request.query.queryItemValue(QStringLiteral("lz"));
    if (lz.isEmpty()) {
        response.status = 500; response.setHeader("Content-Type", "text/plain");
        response.body = "Cannot parse JSON data, err 2"; return response;
    }
    const auto decoded = decompressLzEncodedURIComponent(lz);
    QJsonParseError parseError;
    const QJsonDocument doc = decoded ? QJsonDocument::fromJson(decoded->toUtf8(), &parseError) : QJsonDocument();
    if (!decoded || parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        response.status = 500; response.setHeader("Content-Type", "text/plain");
        response.body = "Cannot parse JSON data, err 3"; return response;
    }
    const QJsonObject object = doc.object();
    const QJsonArray urls = object.value(QStringLiteral("urls")).toArray();
    if (urls.isEmpty()) {
        response.status = 500; response.setHeader("Content-Type", "text/plain");
        response.body = "Cannot parse JSON data, err 3"; return response;
    }
    const QString key = sha256Hex(lz);
    storeSources(kind, parseSourceArray(urls), key);

    QJsonObject options;
    const QJsonValue mustInclude = object.value(QStringLiteral("fileMustInclude"));
    if (mustInclude.isArray() && !mustInclude.toArray().isEmpty()) options.insert(QStringLiteral("fileMustInclude"), mustInclude);
    const int maxFiles = object.value(QStringLiteral("maxFiles")).toInt();
    if (maxFiles > 0) options.insert(QStringLiteral("maxFiles"), maxFiles);
    if (object.contains(QStringLiteral("fileIdx")) && object.value(QStringLiteral("fileIdx")).toInt(-1) > -1)
        options.insert(QStringLiteral("fileIdx"), object.value(QStringLiteral("fileIdx")).toInt());

    QString location = QStringLiteral("/%1/stream?key=%2").arg(archiveKindName(kind), key);
    if (!options.isEmpty()) {
        const QByteArray json = QJsonDocument(options).toJson(QJsonDocument::Compact);
        location += QStringLiteral("&o=") + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(json)));
    }
    response.status = 302; response.setHeader("Location", location.toUtf8());
    return response;
}

ArchiveService::SelectedEntry ArchiveService::selectEntry(ArchiveKind kind, const StoredArchive &stored,
                                                          const ArchiveOptions &options, RemoteError *error,
                                                          CancellationToken *cancel)
{
    SelectedEntry result;
    if (cancel && cancel->isCancelled()) {
        if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
        return result;
    }
    if (kind == ArchiveKind::Rar || kind == ArchiveKind::SevenZip) {
        const QString tool = archiveToolPath();
        if (tool.isEmpty()) {
            if (error) *error = {RemoteErrorCode::Unsupported, QStringLiteral("libarchive-compatible extractor unavailable"), 0};
            return result;
        }
        QTemporaryDir dir;
        if (!dir.isValid()) {
            if (error) *error = {RemoteErrorCode::Transport, QStringLiteral("cannot create archive temp directory"), 0};
            return result;
        }
        QString firstPath;
        int partIndex = 0;
        for (const SourceSpec &spec : stored.sources) {
            RemoteError sourceError;
            auto source = sourceFor(spec, &sourceError);
            if (!source || !sourceError.ok()) { if (error) *error = sourceError; return result; }
            QString fileName = fallbackName(spec.url, kind);
            if (fileName.isEmpty()) fileName = QStringLiteral("part-%1").arg(partIndex);
            const QString path = dir.filePath(fileName);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                if (error) *error = {RemoteErrorCode::Transport, QStringLiteral("cannot materialize archive volume"), 0};
                return result;
            }
            if (!materializeSource(*source, file, &sourceError, cancel)) {
                if (error) *error = sourceError;
                return result;
            }
            file.close();
            if (firstPath.isEmpty()) firstPath = path;
            ++partIndex;
        }
        RemoteError listError;
        const QByteArray listing = processOutput(tool, {QStringLiteral("-tf"), firstPath}, &listError, cancel);
        if (!listError.ok()) { if (error) *error = listError; return result; }
        const QList<QByteArray> lines = listing.split('\n');
        int fileIndex = -1;
        QString selected;
        for (QByteArray line : lines) {
            line = line.trimmed(); if (line.isEmpty() || line.endsWith('/')) continue;
            ++fileIndex;
            const QString name = QString::fromUtf8(line);
            if (matchesOptions(name, fileIndex, options)) { selected = name; break; }
        }
        if (selected.isEmpty()) {
            if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("no file matching archive options"), 0};
            return result;
        }
        RemoteError extractError;
        const QByteArray extracted = processOutput(tool, {QStringLiteral("-xOf"), firstPath, selected}, &extractError, cancel);
        if (!extractError.ok()) { if (error) *error = extractError; return result; }
        result.name = selected; result.size = extracted.size(); result.materialized = extracted;
        result.valid = true; result.directRange = false;
        if (error) *error = {};
        return result;
    }

    RemoteError sourceError;
    auto source = mergedSource(stored, &sourceError);
    if (!source || !sourceError.ok()) { if (error) *error = sourceError; return result; }

    if (kind == ArchiveKind::Zip) {
        const qint64 total = source->length(&sourceError, cancel);
        if (!sourceError.ok() || total < 22) { if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("zip too small"), 0}; return result; }
        const qint64 tailStart = std::max<qint64>(0, total - 65557);
        RemoteRead tailRead = source->read(tailStart, total - 1, cancel);
        if (!tailRead.error.ok()) { if (error) *error = tailRead.error; return result; }
        const QByteArray eocdSig = QByteArray::fromHex("504b0506");
        const qsizetype eocd = tailRead.bytes.lastIndexOf(eocdSig);
        if (eocd < 0 || eocd + 22 > tailRead.bytes.size()) { if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("zip EOCD missing"), 0}; return result; }
        const quint16 count = readLe16(tailRead.bytes, eocd + 10);
        const quint32 centralSize = readLe32(tailRead.bytes, eocd + 12);
        const quint32 centralOffset = readLe32(tailRead.bytes, eocd + 16);
        if (qint64(centralOffset) + centralSize > total) { if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("zip central directory outside source"), 0}; return result; }
        RemoteRead centralRead = source->read(centralOffset, qint64(centralOffset) + centralSize - 1, cancel);
        if (!centralRead.error.ok()) { if (error) *error = centralRead.error; return result; }
        qsizetype pos = 0; int fileIndex = -1;
        for (quint16 i = 0; i < count && pos + 46 <= centralRead.bytes.size(); ++i) {
            if (readLe32(centralRead.bytes, pos) != 0x02014b50) break;
            const quint16 flags = readLe16(centralRead.bytes, pos + 8);
            const quint16 method = readLe16(centralRead.bytes, pos + 10);
            const quint32 compressedSize = readLe32(centralRead.bytes, pos + 20);
            const quint32 uncompressedSize = readLe32(centralRead.bytes, pos + 24);
            const quint16 nameLen = readLe16(centralRead.bytes, pos + 28);
            const quint16 extraLen = readLe16(centralRead.bytes, pos + 30);
            const quint16 commentLen = readLe16(centralRead.bytes, pos + 32);
            const quint32 localOffset = readLe32(centralRead.bytes, pos + 42);
            if (pos + 46 + nameLen + extraLen + commentLen > centralRead.bytes.size()) break;
            const QString entryName = QString::fromUtf8(centralRead.bytes.mid(pos + 46, nameLen));
            pos += 46 + nameLen + extraLen + commentLen;
            if (entryName.endsWith('/')) continue;
            ++fileIndex;
            if (!matchesOptions(entryName, fileIndex, options)) continue;
            if (flags & 0x1) { if (error) *error = {RemoteErrorCode::Unsupported, QStringLiteral("encrypted zip entry unsupported"), 0}; return result; }
            RemoteRead local = source->read(localOffset, qint64(localOffset) + 29, cancel);
            if (!local.error.ok() || local.bytes.size() < 30 || readLe32(local.bytes, 0) != 0x04034b50) {
                if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("zip local header invalid"), 0}; return result;
            }
            const quint16 localNameLen = readLe16(local.bytes, 26);
            const quint16 localExtraLen = readLe16(local.bytes, 28);
            const qint64 dataOffset = qint64(localOffset) + 30 + localNameLen + localExtraLen;
            result.name = entryName; result.size = uncompressedSize; result.valid = true;
            if (method == 0) {
                result.source = source; result.sourceOffset = dataOffset; result.directRange = true;
            } else if (method == 8) {
                if (compressedSize == 0 && uncompressedSize == 0) result.materialized.clear();
                else {
                    RemoteRead compressed = source->read(dataOffset, dataOffset + compressedSize - 1, cancel);
                    if (!compressed.error.ok()) { if (error) *error = compressed.error; return {}; }
                    RemoteError inflateError;
                    result.materialized = inflateRaw(compressed.bytes, uncompressedSize, &inflateError);
                    if (!inflateError.ok()) { if (error) *error = inflateError; return {}; }
                }
                result.compressedZip = true;
            } else {
                if (error) *error = {RemoteErrorCode::Unsupported, QStringLiteral("zip compression method unsupported"), 0};
                return {};
            }
            if (error) *error = {};
            return result;
        }
        if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("no file matching archive options"), 0};
        return result;
    }

    auto selectTar = [&](std::shared_ptr<RemoteRangeSource> tarSource) -> SelectedEntry {
        SelectedEntry selected;
        RemoteError localError;
        const qint64 total = tarSource->length(&localError, cancel);
        if (!localError.ok()) { if (error) *error = localError; return selected; }
        qint64 offset = 0; int fileIndex = -1;
        while (offset + 512 <= total) {
            RemoteRead headerRead = tarSource->read(offset, offset + 511, cancel);
            if (!headerRead.error.ok()) { if (error) *error = headerRead.error; return {}; }
            const QByteArray &h = headerRead.bytes;
            bool allZero = true; for (char c : h) if (c != '\0') { allZero = false; break; }
            if (allZero) break;
            QByteArray nameBytes = h.left(100); const int nameNul = nameBytes.indexOf('\0'); if (nameNul >= 0) nameBytes.truncate(nameNul);
            const QString name = QString::fromUtf8(nameBytes);
            const qint64 size = parseOctal(h.mid(124, 12));
            const char type = h[156];
            if (size < 0) { if (error) *error = {RemoteErrorCode::Parser, QStringLiteral("invalid tar size"), 0}; return {}; }
            if (type == '0' || type == '\0') {
                ++fileIndex;
                if (matchesOptions(name, fileIndex, options)) {
                    selected.name = name; selected.size = size; selected.sourceOffset = offset + 512;
                    selected.source = tarSource; selected.directRange = true; selected.valid = true;
                    if (error) *error = {};
                    return selected;
                }
            }
            offset += 512 + ((size + 511) / 512) * 512;
        }
        if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("no file matching archive options"), 0};
        return selected;
    };

    if (kind == ArchiveKind::Tar) return selectTar(source);

    if (kind == ArchiveKind::Tgz) {
        QByteArray compressed = readAll(*source, &sourceError, cancel);
        if (!sourceError.ok()) { if (error) *error = sourceError; return result; }
        QByteArray plain = gunzip(compressed, &sourceError);
        if (!sourceError.ok()) { if (error) *error = sourceError; return result; }
        const QString firstName = stored.sources.isEmpty() ? QString() : stored.sources.first().url.path().toLower();
        if (firstName.endsWith(QStringLiteral(".tar.gz")) || firstName.endsWith(QStringLiteral(".tgz"))) {
            auto tarMemory = std::make_shared<MemoryRangeSource>(QStringLiteral("inflated.tar"), plain);
            SelectedEntry selected = selectTar(tarMemory);
            if (selected.valid) {
                RemoteRead bytes = selected.source->read(selected.sourceOffset,
                    selected.size ? std::optional<qint64>(selected.sourceOffset + selected.size - 1) : std::nullopt, cancel);
                if (selected.size == 0) bytes.bytes.clear();
                if (!bytes.error.ok() && selected.size > 0) { if (error) *error = bytes.error; return {}; }
                selected.materialized = bytes.bytes; selected.source.reset(); selected.sourceOffset = 0; selected.directRange = false;
            }
            return selected;
        }
        result.name = QStringLiteral("video"); result.size = plain.size(); result.materialized = plain;
        result.valid = true; result.directRange = false; if (error) *error = {}; return result;
    }

    if (error) *error = {RemoteErrorCode::Unsupported, QStringLiteral("archive kind unsupported"), 0};
    return result;
}

Response ArchiveService::handleStream(ArchiveKind kind, const Request &request)
{
    Response response;
    const QString key = request.query.queryItemValue(QStringLiteral("key"));
    const auto it = m_store.constFind(storeKey(kind, key));
    if (key.isEmpty() || it == m_store.cend()) { response.status = 500; return response; }
    RemoteError optionError;
    const ArchiveOptions options = parseArchiveOptions(request.query.queryItemValue(QStringLiteral("o")), &optionError);
    if (!optionError.ok()) { response.status = 500; return response; }
    RemoteError selectionError;
    CancellationToken *cancel = request.cancellation.get();
    SelectedEntry entry = selectEntry(kind, *it, options, &selectionError, cancel);
    if (!entry.valid || !selectionError.ok()) {
        response.status = 500;
        if (selectionError.code == RemoteErrorCode::Parser) {
            const QString message = QStringLiteral("There was an error with the %1 parser.").arg(archiveKindName(kind));
            response.body = message.toUtf8();
        }
        return response;
    }
    const QByteArray mime = entry.name == QStringLiteral("video") ? QByteArray("application/octet-stream") : contentTypeForName(entry.name);
    const QByteArray rangeRaw = request.header("Range");

    if (kind == ArchiveKind::Tgz) {
        const ParsedRange range = parseHttpRange(rangeRaw, entry.size);
        if (range.present) {
            const QByteArray exactFull = "bytes=0-" + QByteArray::number(entry.size - 1);
            if (rangeRaw != "bytes=0-" && rangeRaw != exactFull) { response.status = 405; return response; }
        }
        response.setHeader("Content-Type", mime);
        if (entry.size > 0) response.setHeader("Content-Length", QByteArray::number(entry.size));
        response.setHeader("Accept-Ranges", "none");
        if (request.method == "HEAD") { response.status = 204; return response; }
        response.status = 200; response.body = entry.materialized; return response;
    }

    // RAR and 7z upstream return 204 for HEAD regardless of a Range header.
    if ((kind == ArchiveKind::Rar || kind == ArchiveKind::SevenZip) && request.method == "HEAD") {
        response.status = 204; response.setHeader("Accept-Ranges", "bytes");
        response.setHeader("Content-Length", QByteArray::number(entry.size)); response.setHeader("Content-Type", mime);
        return response;
    }

    ParsedRange range = parseHttpRange(rangeRaw, entry.size);
    if (range.present && !range.valid) {
        response.status = 416; response.setHeader("Content-Range", "bytes */" + QByteArray::number(entry.size)); return response;
    }
    const qint64 start = range.present ? range.start : 0;
    const qint64 end = range.present ? range.end : entry.size - 1;
    response.status = range.present ? 206 : 200;
    response.setHeader("Accept-Ranges", "bytes");
    response.setHeader("Content-Type", mime);
    response.setHeader("Content-Length", QByteArray::number(std::max<qint64>(0, end - start + 1)));
    response.setHeader("transferMode.dlna.org", kDlnaTransferMode);
    response.setHeader("contentFeatures.dlna.org", kDlnaFeatures);
    if (range.present) response.setHeader("Content-Range", "bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(entry.size));
    if (request.method == "HEAD") return response;
    if (entry.size == 0) return response;
    if (entry.directRange && entry.source) {
        RemoteRead bytes = entry.source->read(entry.sourceOffset + start, entry.sourceOffset + end, cancel);
        if (!bytes.error.ok()) { response.status = 500; response.body.clear(); return response; }
        response.body = bytes.bytes;
    } else {
        response.body = entry.materialized.mid(start, end - start + 1);
    }
    return response;
}

} // namespace Colosseum::Server::RemoteArchive
