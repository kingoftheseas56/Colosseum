#include "ProtocolFixtures.h"
#include "RemoteArchive.h"
#include "RemoteServices.h"
#include "miniz.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Colosseum::Server::RemoteArchive;

namespace {

void appendLe16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

void appendLe32(QByteArray &out, quint32 value)
{
    for (int i = 0; i < 4; ++i) out.append(char((value >> (i * 8)) & 0xff));
}

QByteArray makeStoredZip(const QByteArray &name, const QByteArray &payload)
{
    const quint32 crc = static_cast<quint32>(mz_crc32(MZ_CRC32_INIT,
        reinterpret_cast<const unsigned char *>(payload.constData()), payload.size()));
    QByteArray out;
    const quint32 localOffset = 0;
    appendLe32(out, 0x04034b50);
    appendLe16(out, 20); appendLe16(out, 0); appendLe16(out, 0);
    appendLe16(out, 0); appendLe16(out, 0);
    appendLe32(out, crc); appendLe32(out, payload.size()); appendLe32(out, payload.size());
    appendLe16(out, name.size()); appendLe16(out, 0);
    out += name; out += payload;
    const quint32 centralOffset = out.size();
    appendLe32(out, 0x02014b50);
    appendLe16(out, 20); appendLe16(out, 20); appendLe16(out, 0); appendLe16(out, 0);
    appendLe16(out, 0); appendLe16(out, 0);
    appendLe32(out, crc); appendLe32(out, payload.size()); appendLe32(out, payload.size());
    appendLe16(out, name.size()); appendLe16(out, 0); appendLe16(out, 0); appendLe16(out, 0);
    appendLe16(out, 0); appendLe32(out, 0); appendLe32(out, localOffset);
    out += name;
    const quint32 centralSize = out.size() - centralOffset;
    appendLe32(out, 0x06054b50);
    appendLe16(out, 0); appendLe16(out, 0); appendLe16(out, 1); appendLe16(out, 1);
    appendLe32(out, centralSize); appendLe32(out, centralOffset); appendLe16(out, 0);
    return out;
}

QByteArray tarOctal(qint64 value, int width)
{
    QByteArray out = QByteArray::number(value, 8).rightJustified(width - 1, '0');
    out.append('\0');
    return out;
}

QByteArray makeTar(const QByteArray &name, const QByteArray &payload)
{
    QByteArray header(512, '\0');
    auto put = [&](int off, const QByteArray &v) { memcpy(header.data() + off, v.constData(), qMin(v.size(), 512 - off)); };
    put(0, name); put(100, "0000644\0"); put(108, "0000000\0"); put(116, "0000000\0");
    put(124, tarOctal(payload.size(), 12)); put(136, tarOctal(0, 12));
    memset(header.data() + 148, ' ', 8); header[156] = '0';
    put(257, "ustar\0"); put(263, "00");
    quint64 sum = 0; for (unsigned char c : header) sum += c;
    QByteArray checksum = QByteArray::number(sum, 8).rightJustified(6, '0') + QByteArray("\0 ", 2);
    put(148, checksum);
    QByteArray out = header + payload;
    const int pad = (512 - (payload.size() % 512)) % 512;
    out += QByteArray(pad, '\0'); out += QByteArray(1024, '\0');
    return out;
}

QByteArray gzipBytes(const QByteArray &plain)
{
    QByteArray out;
    out.append(QByteArray::fromHex("1f8b08000000000000ff"));
    size_t compressedSize = 0;
    void *compressed = tdefl_compress_mem_to_heap(plain.constData(), plain.size(), &compressedSize,
                                                   TDEFL_DEFAULT_MAX_PROBES);
    // tdefl_compress_mem_to_heap returns a zlib stream unless told otherwise. Use the
    // low-level compressor to obtain raw deflate bytes for a real gzip member.
    if (compressed) mz_free(compressed);
    mz_stream stream{};
    if (mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED,
                        -MZ_DEFAULT_WINDOW_BITS, 8, MZ_DEFAULT_STRATEGY) != MZ_OK) {
        return {};
    }
    QByteArray raw(qMax(128, plain.size() * 2 + 64), '\0');
    stream.next_in = reinterpret_cast<const unsigned char *>(plain.constData());
    stream.avail_in = static_cast<unsigned int>(plain.size());
    stream.next_out = reinterpret_cast<unsigned char *>(raw.data());
    stream.avail_out = static_cast<unsigned int>(raw.size());
    if (mz_deflate(&stream, MZ_FINISH) != MZ_STREAM_END) {
        mz_deflateEnd(&stream);
        return {};
    }
    raw.resize(raw.size() - stream.avail_out);
    mz_deflateEnd(&stream);
    out += raw;
    appendLe32(out, static_cast<quint32>(mz_crc32(MZ_CRC32_INIT,
        reinterpret_cast<const unsigned char *>(plain.constData()), plain.size())));
    appendLe32(out, static_cast<quint32>(plain.size()));
    return out;
}

class RangeFixtureServer final : public QTcpServer {
public:
    QByteArray payload;
    bool failNext = false;
    QList<QByteArray> seenRanges;

    explicit RangeFixtureServer(QByteArray bytes) : payload(std::move(bytes))
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray req = socket->readAll();
                    if (!req.contains("\r\n\r\n")) return;
                    QByteArray range;
                    for (const QByteArray &line : req.split('\n')) {
                        if (line.toLower().startsWith("range:")) range = line.mid(6).trimmed();
                    }
                    seenRanges.append(range);
                    if (failNext) {
                        failNext = false;
                        socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    if (req.startsWith("HEAD ")) {
                        const QByteArray head = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) +
                            "\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n";
                        socket->write(head); socket->disconnectFromHost(); return;
                    }
                    qint64 start = 0, end = payload.size() - 1; int status = 200;
                    if (range.startsWith("bytes=")) {
                        const QList<QByteArray> parts = range.mid(6).split('-');
                        if (!parts.value(0).isEmpty()) start = parts.value(0).toLongLong();
                        if (!parts.value(1).isEmpty()) end = parts.value(1).toLongLong();
                        status = 206;
                    }
                    if (start >= payload.size() || end < start) {
                        socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" +
                                      QByteArray::number(payload.size()) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost(); return;
                    }
                    end = qMin<qint64>(end, payload.size() - 1);
                    const QByteArray body = payload.mid(start, end - start + 1);
                    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + (status == 206 ? " Partial Content\r\n" : " OK\r\n");
                    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nAccept-Ranges: bytes\r\n";
                    if (status == 206) head += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(payload.size()) + "\r\n";
                    head += "Connection: close\r\n\r\n";
                    socket->write(head + body); socket->disconnectFromHost();
                });
            }
        });
        QVERIFY(listen(QHostAddress::LocalHost, 0));
    }

    QUrl url(const QString &path = QStringLiteral("/fixture.bin")) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }
};

QByteArray header(const Response &r, const QByteArray &name)
{
    return r.header(name);
}

Request makeRequest(QByteArray method, QString path)
{
    Request request;
    request.method = std::move(method);
    request.path = std::move(path);
    return request;
}

class FakeFtpBackend final : public FtpBackend {
public:
    QByteArray payload = "abcdefghijklmnopqrstuvwxyz";
    FtpProbe probeResult{payload.size(), QStringLiteral("video/mp4"), QDateTime::fromSecsSinceEpoch(1234), true};
    FtpProbe probe(const QUrl &, RemoteError *error) override { if (error) *error = {}; return probeResult; }
    RemoteRead read(const QUrl &, qint64 start, std::optional<qint64> end, CancellationToken *) override
    {
        RemoteRead r; r.totalLength = payload.size(); r.start = start;
        r.end = end.value_or(payload.size() - 1); r.bytes = payload.mid(start, r.end - start + 1); return r;
    }
};

class FakeNzbFetcher final : public NzbDocumentFetcher {
public:
    QByteArray xml;
    QByteArray fetch(const QUrl &, RemoteError *error) override { if (error) *error = {}; return xml; }
};

class FakeNntpBackend final : public NntpBackend {
public:
    QHash<QString, QByteArray> articles;
    QByteArray fetch(const NntpEndpoint &, const NzbSegment &segment, RemoteError *error) override
    {
        if (!articles.contains(segment.article)) {
            if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("missing article"), 0};
            return {};
        }
        if (error) *error = {};
        return articles.value(segment.article);
    }
};

class CancellingRangeSource final : public RemoteRangeSource {
public:
    explicit CancellingRangeSource(QByteArray bytes) : bytes_(std::move(bytes)) {}

    qint64 length(RemoteError *error, CancellationToken *cancel) override
    {
        if (cancel && cancel->isCancelled()) {
            if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
            return -1;
        }
        if (error) *error = {};
        return bytes_.size();
    }

    RemoteRead read(qint64 start, std::optional<qint64> end,
                    CancellationToken *cancel) override
    {
        RemoteRead result;
        result.totalLength = bytes_.size();
        result.start = start;
        result.end = end.value_or(bytes_.size() - 1);
        ranges.push_back({start, result.end});
        if (cancel && cancel->isCancelled()) {
            result.error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
            return result;
        }
        result.bytes = bytes_.mid(start, result.end - start + 1);
        if (ranges.size() == 1 && cancel)
            cancel->cancel();
        return result;
    }

    QString name() const override { return QStringLiteral("large.rar"); }
    QList<QPair<qint64, qint64>> ranges;

private:
    QByteArray bytes_;
};

} // namespace

class RemoteArchiveTests : public QObject {
    Q_OBJECT
private slots:
    void httpRangeSourceBoundedOpenEndedAndRetry();
    void concatenatedSourceCrossesVolumeBoundary();
    void zipMatchesWave0StoredEntryContract();
    void tarMatchesWave0RangeContract();
    void tgzRejectsPartialRangeLikeOracle();
    void archiveCreatePreservesMultipartSortAndFileIndex();
    void sevenZipUsesRealArchiveFixtureWhenAvailable();
    void rarInvalidFixtureMapsParserError();
    void archiveVolumeMaterializationIsChunkedAndCancellable();
    void lzBootstrapRoutesMatchUpstream();
    void ftpBackendLocalhostProtocol();
    void ftpCreateRedirectAndRangeContract();
    void nntpBackendLocalhostYencProtocol();
    void nzbDirectVideoRangeAndMissingThreshold();
};

void RemoteArchiveTests::httpRangeSourceBoundedOpenEndedAndRetry()
{
    const QByteArray payload = QByteArray::fromRawData("0123456789abcdefghijklmnopqrstuvwxyz", 36);
    RangeFixtureServer server(payload);
    QNetworkAccessManager nam;
    HttpRangeSource source(&nam, server.url());
    RemoteError error;
    QCOMPARE(source.length(&error), qint64(payload.size()));
    QVERIFY(error.ok());

    RemoteRead bounded = source.read(7, 13, nullptr);
    QVERIFY(bounded.error.ok());
    QCOMPARE(bounded.bytes, payload.mid(7, 7));
    QCOMPARE(server.seenRanges.last(), QByteArray("bytes=7-13"));

    RemoteRead open = source.read(30, std::nullopt, nullptr);
    QVERIFY(open.error.ok());
    QCOMPARE(open.bytes, payload.mid(30));
    QCOMPARE(server.seenRanges.last(), QByteArray("bytes=30-"));

    server.failNext = true;
    RemoteRead retried = source.read(2, 4, nullptr);
    QVERIFY2(retried.error.ok(), qPrintable(retried.error.message));
    QCOMPARE(retried.bytes, payload.mid(2, 3));

    const qsizetype requestsBeforeCancel = server.seenRanges.size();
    CancellationToken cancelled;
    cancelled.cancel();
    RemoteRead cancelledRead = source.read(0, 1, &cancelled);
    QVERIFY(cancelledRead.error.code == RemoteErrorCode::Cancelled);
    QCOMPARE(server.seenRanges.size(), requestsBeforeCancel);
}

void RemoteArchiveTests::concatenatedSourceCrossesVolumeBoundary()
{
    QVector<std::shared_ptr<RemoteRangeSource>> pieces;
    pieces << std::make_shared<MemoryRangeSource>(QStringLiteral("a"), QByteArray("ABCDE"));
    pieces << std::make_shared<MemoryRangeSource>(QStringLiteral("b"), QByteArray("FGHIJ"));
    ConcatenatedRangeSource merged(QStringLiteral("merged"), pieces);
    RemoteRead read = merged.read(3, 7, nullptr);
    QVERIFY(read.error.ok());
    QCOMPARE(read.bytes, QByteArray("DEFGH"));
    QCOMPARE(read.totalLength, qint64(10));
}

void RemoteArchiveTests::zipMatchesWave0StoredEntryContract()
{
    const QByteArray movie = QByteArray("wave0-movie-bytes-") + QByteArray(4000, 'z');
    const QByteArray archive = makeStoredZip("movie.mp4", movie);
    QHash<QString, QByteArray> payloads{{QStringLiteral("http://fixture/sample.zip"), archive}};
    ArchiveService service([&](const SourceSpec &spec, RemoteError *error) {
        if (error) *error = {};
        return std::make_shared<MemoryRangeSource>(spec.url.fileName(), payloads.value(spec.url.toString()));
    });

    Request create = makeRequest("POST", QStringLiteral("/create/wave0-zip"));
    create.body = QJsonDocument(QJsonArray{QStringLiteral("http://fixture/sample.zip")}).toJson(QJsonDocument::Compact);
    Response created = service.handle(ArchiveKind::Zip, create);
    QCOMPARE(created.status, 200);
    QCOMPARE(QJsonDocument::fromJson(created.body).object().value(QStringLiteral("key")).toString(), QStringLiteral("wave0-zip"));

    Request headReq = makeRequest("HEAD", QStringLiteral("/stream"));
    headReq.query.addQueryItem(QStringLiteral("key"), QStringLiteral("wave0-zip"));
    headReq.query.addQueryItem(QStringLiteral("o"), QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("fileMustInclude"), QJsonArray{QStringLiteral("movie.mp4")}}}).toJson(QJsonDocument::Compact)));
    Response headResp = service.handle(ArchiveKind::Zip, headReq);
    QCOMPARE(headResp.status, 200);
    QCOMPARE(header(headResp, "Accept-Ranges"), QByteArray("bytes"));
    QCOMPARE(header(headResp, "Content-Length"), QByteArray::number(movie.size()));
    QCOMPARE(header(headResp, "Content-Type"), QByteArray("video/mp4"));
    QVERIFY(headResp.body.isEmpty());

    Request rangeReq = headReq; rangeReq.method = "GET"; rangeReq.headers.insert("Range", "bytes=0-127");
    Response rangeResp = service.handle(ArchiveKind::Zip, rangeReq);
    QCOMPARE(rangeResp.status, 206);
    QCOMPARE(header(rangeResp, "Content-Range"), QByteArray("bytes 0-127/") + QByteArray::number(movie.size()));
    QCOMPARE(rangeResp.body, movie.left(128));
}

void RemoteArchiveTests::tarMatchesWave0RangeContract()
{
    const QByteArray movie = QByteArray("tar-payload-") + QByteArray(4096, '\x33');
    const QByteArray archive = makeTar("movie.mp4", movie);
    ArchiveService service([&](const SourceSpec &, RemoteError *error) {
        if (error) *error = {};
        return std::make_shared<MemoryRangeSource>(QStringLiteral("sample.tar"), archive);
    });
    Request create = makeRequest("POST", QStringLiteral("/create/wave0-tar"));
    create.body = "[\"http://fixture/sample.tar\"]";
    QCOMPARE(service.handle(ArchiveKind::Tar, create).status, 200);
    Request request = makeRequest("GET", QStringLiteral("/stream"));
    request.query.addQueryItem(QStringLiteral("key"), QStringLiteral("wave0-tar"));
    request.headers.insert("Range", "bytes=5-16");
    Response response = service.handle(ArchiveKind::Tar, request);
    QCOMPARE(response.status, 206);
    QCOMPARE(response.body, movie.mid(5, 12));
    QCOMPARE(header(response, "Accept-Ranges"), QByteArray("bytes"));
}

void RemoteArchiveTests::tgzRejectsPartialRangeLikeOracle()
{
    const QByteArray movie = QByteArray("tgz-payload-") + QByteArray(4096, '\x55');
    const QByteArray archive = gzipBytes(makeTar("movie.mp4", movie));
    ArchiveService service([&](const SourceSpec &, RemoteError *error) {
        if (error) *error = {};
        return std::make_shared<MemoryRangeSource>(QStringLiteral("sample.tgz"), archive);
    });
    Request create = makeRequest("POST", QStringLiteral("/create/wave0-tgz"));
    create.body = "[\"http://fixture/sample.tgz\"]";
    QCOMPARE(service.handle(ArchiveKind::Tgz, create).status, 200);

    Request headReq = makeRequest("HEAD", QStringLiteral("/stream"));
    headReq.query.addQueryItem(QStringLiteral("key"), QStringLiteral("wave0-tgz"));
    Response headResp = service.handle(ArchiveKind::Tgz, headReq);
    QCOMPARE(headResp.status, 204);
    QCOMPARE(header(headResp, "Accept-Ranges"), QByteArray("none"));
    QCOMPARE(header(headResp, "Content-Length"), QByteArray::number(movie.size()));

    Request partial = headReq; partial.method = "GET"; partial.headers.insert("Range", "bytes=0-127");
    Response rejected = service.handle(ArchiveKind::Tgz, partial);
    QCOMPARE(rejected.status, 405);
    QVERIFY(rejected.body.isEmpty());

    Request full = headReq; full.method = "GET"; full.headers.insert("Range", "bytes=0-");
    Response whole = service.handle(ArchiveKind::Tgz, full);
    QCOMPARE(whole.status, 200);
    QCOMPARE(whole.body, movie);
}

void RemoteArchiveTests::archiveCreatePreservesMultipartSortAndFileIndex()
{
    QHash<QString, QByteArray> volumes;
    QByteArray first = makeTar("notes.txt", "skip");
    first.chop(1024); // remove the first archive's end markers to form one valid two-entry TAR
    const QByteArray tar = first + makeTar("movie.mp4", "chosen");
    const int split = tar.size() / 2;
    volumes.insert(QStringLiteral("http://fixture/title.tar.2"), tar.mid(split));
    volumes.insert(QStringLiteral("http://fixture/title.tar.1"), tar.left(split));
    ArchiveService service([&](const SourceSpec &spec, RemoteError *error) {
        if (error) *error = {};
        return std::make_shared<MemoryRangeSource>(spec.url.fileName(), volumes.value(spec.url.toString()));
    });
    Request create = makeRequest("POST", QStringLiteral("/create/multi"));
    create.body = "[\"http://fixture/title.tar.2\",\"http://fixture/title.tar.1\"]";
    QCOMPARE(service.handle(ArchiveKind::Tar, create).status, 200);
    QCOMPARE(service.sourcesForTesting(ArchiveKind::Tar, QStringLiteral("multi")).first().url.toString(), QStringLiteral("http://fixture/title.tar.1"));

    Request indexed = makeRequest("GET", QStringLiteral("/stream"));
    indexed.query.addQueryItem(QStringLiteral("key"), QStringLiteral("multi"));
    indexed.query.addQueryItem(QStringLiteral("o"), QStringLiteral(R"({"fileIdx":1})"));
    Response chosen = service.handle(ArchiveKind::Tar, indexed);
    QCOMPARE(chosen.status, 200);
    QCOMPARE(chosen.body, QByteArray("chosen"));
}

void RemoteArchiveTests::sevenZipUsesRealArchiveFixtureWhenAvailable()
{
    const QString fixture = QFINDTESTDATA("fixtures/sample.7z");
    if (fixture.isEmpty()) QSKIP("real 7z fixture unavailable");
    QFile file(fixture); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray bytes = file.readAll();
    ArchiveService service([&](const SourceSpec &, RemoteError *error) {
        if (error) *error = {};
        return std::make_shared<MemoryRangeSource>(QStringLiteral("sample.7z"), bytes);
    });
    Request create = makeRequest("POST", QStringLiteral("/create/seven")); create.body = "[\"http://fixture/sample.7z\"]";
    QCOMPARE(service.handle(ArchiveKind::SevenZip, create).status, 200);
    Request stream = makeRequest("GET", QStringLiteral("/stream")); stream.query.addQueryItem(QStringLiteral("key"), QStringLiteral("seven"));
    const QByteArray expected("sevenzip-real-fixture");
    Response response = service.handle(ArchiveKind::SevenZip, stream);
    QVERIFY2(response.status == 200, qPrintable(QString::fromUtf8(response.body)));
    QCOMPARE(response.body, expected);

    Request head = stream; head.method = "HEAD";
    Response headResponse = service.handle(ArchiveKind::SevenZip, head);
    QCOMPARE(headResponse.status, 204);
    QCOMPARE(header(headResponse, "Accept-Ranges"), QByteArray("bytes"));
    QCOMPARE(header(headResponse, "Content-Length"), QByteArray::number(expected.size()));

    Request range = stream; range.headers.insert("Range", "bytes=1-5");
    Response ranged = service.handle(ArchiveKind::SevenZip, range);
    QCOMPARE(ranged.status, 206);
    QCOMPARE(ranged.body, expected.mid(1, 5));
    QCOMPARE(header(ranged, "Content-Range"), QByteArray("bytes 1-5/") + QByteArray::number(expected.size()));
}

void RemoteArchiveTests::rarInvalidFixtureMapsParserError()
{
    ArchiveService service([](const SourceSpec &, RemoteError *error) {
        if (error) *error = {};
        return std::make_shared<MemoryRangeSource>(QStringLiteral("broken.rar"), QByteArray("not-a-rar-archive"));
    });
    Request create = makeRequest("POST", QStringLiteral("/create/broken-rar"));
    create.body = "[\"http://fixture/broken.rar\"]";
    QCOMPARE(service.handle(ArchiveKind::Rar, create).status, 200);
    Request stream = makeRequest("GET", QStringLiteral("/stream"));
    stream.query.addQueryItem(QStringLiteral("key"), QStringLiteral("broken-rar"));
    Response failed = service.handle(ArchiveKind::Rar, stream);
    QCOMPARE(failed.status, 500);
    QCOMPARE(failed.body, QByteArray("There was an error with the rar parser."));
}

void RemoteArchiveTests::archiveVolumeMaterializationIsChunkedAndCancellable()
{
    auto source = std::make_shared<CancellingRangeSource>(QByteArray(2 * 1024 * 1024 + 17, 'r'));
    ArchiveService service([source](const SourceSpec &, RemoteError *error) {
        if (error) *error = {};
        return source;
    });

    Request create = makeRequest("POST", QStringLiteral("/create/chunked-rar"));
    create.body = "[\"http://fixture/large.rar\"]";
    QCOMPARE(service.handle(ArchiveKind::Rar, create).status, 200);

    Request stream = makeRequest("GET", QStringLiteral("/stream"));
    stream.query.addQueryItem(QStringLiteral("key"), QStringLiteral("chunked-rar"));
    stream.cancellation = std::make_shared<CancellationToken>();
    const Response cancelled = service.handle(ArchiveKind::Rar, stream);
    QCOMPARE(cancelled.status, 500);
    QCOMPARE(source->ranges.size(), 1);
    QCOMPARE(source->ranges.first().first, qint64(0));
    QCOMPARE(source->ranges.first().second, qint64(1024 * 1024 - 1));
}

void RemoteArchiveTests::lzBootstrapRoutesMatchUpstream()
{
    const QString archiveLz = QStringLiteral("N4IgrgTgNgziBcBtEALALmgDvA9DgZgJYAeakApjjAIYC2mU5AdAF6GYgC6ANCEYwFkwMNAEkAdgGMoYACbkEyWgHsAboWb0ALF179yo2cQQAGXrWrEAYoUZx4ARgC+QA");
    const QString ftpLz = QStringLiteral("N4IgZgLgDgqgTgGxALnNZB6DBXAzgUzmSgENdcABfADxIFsoF8A6CfXCDOgewDcBLFgwAsIAL5A");
    const QString nzbLz = QStringLiteral("N4IgzgpgTgbtYgFwG0QDs0BcAOiD0eArotgAIQAeAhgLbYA2EAdJhGJogIycCceATCAC6AGnQAvAEYBVKPSQgAFphz48AMwCWFTISgQ8m1jSZopIAL5A");
    auto hash = [](const QString &value) {
        return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
    };

    const QByteArray movie = QByteArray("lz-movie-") + QByteArray(64, 'x');
    const QByteArray zip = makeStoredZip("movie.mp4", movie);
    ArchiveService archive([&](const SourceSpec &, RemoteError *error) {
        if (error) *error = {};
        return std::make_shared<MemoryRangeSource>(QStringLiteral("sample.zip"), zip);
    });
    Request archiveCreate = makeRequest("GET", QStringLiteral("/create"));
    archiveCreate.query.addQueryItem(QStringLiteral("lz"), archiveLz);
    Response archiveRedirect = archive.handle(ArchiveKind::Zip, archiveCreate);
    QCOMPARE(archiveRedirect.status, 302);
    const QString archiveLocation = QString::fromUtf8(header(archiveRedirect, "Location"));
    QVERIFY(archiveLocation.startsWith(QStringLiteral("/zip/stream?key=") + hash(archiveLz)));
    QVERIFY(archiveLocation.contains(QStringLiteral("&o=")));

    auto ftpBackend = std::make_shared<FakeFtpBackend>();
    FtpService ftp(ftpBackend);
    Request ftpCreate = makeRequest("GET", QStringLiteral("/create"));
    ftpCreate.query.addQueryItem(QStringLiteral("lz"), ftpLz);
    Response ftpRedirect = ftp.handle(ftpCreate);
    QCOMPARE(ftpRedirect.status, 302);
    QCOMPARE(header(ftpRedirect, "Location"),
             QByteArray("/stream/") + hash(ftpLz).toUtf8() + QByteArray("/movie.mp4"));

    FtpService ftpAlias(ftpBackend);
    Request ftpAliasCreate = ftpCreate;
    ftpAliasCreate.path = QStringLiteral("/movie.mp4");
    Response ftpAliasRedirect = ftpAlias.handle(ftpAliasCreate);
    QCOMPARE(ftpAliasRedirect.status, 302); // module 1306 ALL /:fileName alias
    QCOMPARE(header(ftpAliasRedirect, "Location"), header(ftpRedirect, "Location"));

    auto docs = std::make_shared<FakeNzbFetcher>();
    docs->xml = R"(<?xml version="1.0"?><nzb><file subject="movie.mp4"><groups><group>alt.binaries.test</group></groups><segments><segment bytes="4" number="1">a1</segment><segment bytes="4" number="2">a2</segment></segments></file></nzb>)";
    auto nntp = std::make_shared<FakeNntpBackend>();
    nntp->articles.insert(QStringLiteral("a1"), QByteArray("ABCD"));
    nntp->articles.insert(QStringLiteral("a2"), QByteArray("EFGH"));
    NzbService nzb(docs, nntp);
    Request nzbCreate = makeRequest("GET", QStringLiteral("/create"));
    nzbCreate.query.addQueryItem(QStringLiteral("lz"), nzbLz);
    Response nzbRedirect = nzb.handle(nzbCreate);
    QCOMPARE(nzbRedirect.status, 302);
    QCOMPARE(header(nzbRedirect, "Location"),
             QByteArray("/nzb/stream/") + hash(nzbLz).toUtf8() + QByteArray("/movie.mp4"));
}

void RemoteArchiveTests::ftpBackendLocalhostProtocol()
{
    TestProtocol::FtpFixture fixture(QByteArray("abcdefghijklmnopqrstuvwxyz"));
    QVERIFY(fixture.port() != 0);
    const QUrl url(QStringLiteral("ftp://user:pass@127.0.0.1:%1/movie.mp4").arg(fixture.port()));
    QtFtpBackend backend;
    RemoteError error;
    const FtpProbe probe = backend.probe(url, &error);
    QVERIFY2(error.ok(), qPrintable(error.message));
    QCOMPARE(probe.size, qint64(26));
    QVERIFY(probe.supportsSeek);
    QVERIFY(probe.lastModified.isValid());

    RemoteRead read = backend.read(url, 5, 9, nullptr);
    QVERIFY2(read.error.ok(), qPrintable(read.error.message));
    QCOMPARE(read.totalLength, qint64(26));
    QCOMPARE(read.bytes, QByteArray("fghij"));
    QCOMPARE(read.start, qint64(5));
    QCOMPARE(read.end, qint64(9));
}

void RemoteArchiveTests::ftpCreateRedirectAndRangeContract()
{
    auto backend = std::make_shared<FakeFtpBackend>();
    FtpService service(backend);
    Request create = makeRequest("POST", QStringLiteral("/create/ftp-key"));
    create.body = R"({"ftpUrl":"ftp://user:pass@example.test/movie.mp4"})";
    Response created = service.handle(create);
    QCOMPARE(created.status, 200);
    QCOMPARE(QJsonDocument::fromJson(created.body).object().value(QStringLiteral("key")).toString(), QStringLiteral("ftp-key"));

    Request redirect = makeRequest("GET", QStringLiteral("/stream")); redirect.query.addQueryItem(QStringLiteral("key"), QStringLiteral("ftp-key"));
    Response redirected = service.handle(redirect);
    QCOMPARE(redirected.status, 302);
    QCOMPARE(header(redirected, "Location"), QByteArray("/stream/ftp-key/movie.mp4"));

    Request range = makeRequest("GET", QStringLiteral("/stream/ftp-key/movie.mp4")); range.headers.insert("Range", "bytes=2-7");
    Response ranged = service.handle(range);
    QCOMPARE(ranged.status, 206);
    QCOMPARE(ranged.body, QByteArray("cdefgh"));
    QCOMPARE(header(ranged, "Content-Range"), QByteArray("bytes 2-7/26"));
}

void RemoteArchiveTests::nntpBackendLocalhostYencProtocol()
{
    TestProtocol::NntpFixture fixture(QByteArray("ABCD"));
    QVERIFY(fixture.port() != 0);
    const auto endpoint = parseNntpEndpoint(
        QStringLiteral("nntp://user:pass@127.0.0.1:%1/2").arg(fixture.port()));
    QVERIFY(endpoint.has_value());
    NzbSegment segment;
    segment.number = 1;
    segment.declaredBytes = 4;
    segment.group = QStringLiteral("alt.binaries.test");
    segment.article = QStringLiteral("fixture-article");
    QtNntpBackend backend;
    RemoteError error;
    const QByteArray bytes = backend.fetch(*endpoint, segment, &error);
    QVERIFY2(error.ok(), qPrintable(error.message));
    QCOMPARE(bytes, QByteArray("ABCD"));
}

void RemoteArchiveTests::nzbDirectVideoRangeAndMissingThreshold()
{
    auto docs = std::make_shared<FakeNzbFetcher>();
    docs->xml = R"(<?xml version="1.0"?><nzb><file subject="movie.mp4"><groups><group>alt.binaries.test</group></groups><segments><segment bytes="4" number="1">a1</segment><segment bytes="4" number="2">a2</segment><segment bytes="4" number="3">a3</segment></segments></file></nzb>)";
    auto nntp = std::make_shared<FakeNntpBackend>();
    nntp->articles.insert(QStringLiteral("a1"), QByteArray("ABCD"));
    nntp->articles.insert(QStringLiteral("a2"), QByteArray("EFGH"));
    nntp->articles.insert(QStringLiteral("a3"), QByteArray("IJKL"));
    NzbService service(docs, nntp);
    Request create = makeRequest("POST", QStringLiteral("/create/nzb-key"));
    create.body = R"({"servers":["nntp://u:p@example.test:119/2"],"nzbUrl":"http://fixture/item.nzb"})";
    QCOMPARE(service.handle(create).status, 200);
    Request stream = makeRequest("GET", QStringLiteral("/stream/nzb-key/movie.mp4")); stream.headers.insert("Range", "bytes=3-9");
    Response response = service.handle(stream);
    QCOMPARE(response.status, 200); // Stremio module 1088 intentionally uses 200 for ranged NZB media.
    QCOMPARE(header(response, "Content-Range"), QByteArray("bytes 3-9/12"));
    QCOMPARE(response.body, QByteArray("DEFGHIJ"));

    auto unavailable = std::make_shared<FakeNntpBackend>();
    unavailable->articles = nntp->articles;
    unavailable->articles.remove(QStringLiteral("a2"));
    NzbService zeroFill(docs, unavailable);
    QCOMPARE(zeroFill.handle(create).status, 200);
    Response zeroFilled = zeroFill.handle(stream);
    QCOMPARE(zeroFilled.status, 200);
    QCOMPARE(zeroFilled.body, QByteArray("D\0\0\0\0IJ", 7));

    auto gappedDocs = std::make_shared<FakeNzbFetcher>();
    gappedDocs->xml = R"(<?xml version="1.0"?><nzb><file subject="movie.mp4"><groups><group>alt.binaries.test</group></groups><segments><segment bytes="4" number="1">a1</segment><segment bytes="4" number="3">a3</segment></segments></file></nzb>)";
    NzbService strict(gappedDocs, nntp);
    Response failed = strict.handle(create);
    QCOMPARE(failed.status, 500); // One metadata hole out of three exceeds module 1088's 2% gate.
}

QTEST_MAIN(RemoteArchiveTests)
#include "remote_archive_tests.moc"
