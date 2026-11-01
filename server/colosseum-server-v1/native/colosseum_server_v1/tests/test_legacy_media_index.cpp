#include "server1/media/LegacyMediaIndex.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <memory>
using namespace server1::media;

namespace {
QByteArray number(quint64 value, int width)
{
    QByteArray bytes(width, '\0');
    for (int i = width - 1; i >= 0; --i) { bytes[i] = char(value); value >>= 8; }
    return bytes;
}
QByteArray box(const char* type, const QByteArray& payload, bool extended = false)
{
    return number(extended ? 1 : payload.size() + 8, 4) + QByteArray(type, 4)
        + (extended ? number(payload.size() + 16, 8) : QByteArray{}) + payload;
}
QByteArray moov(bool extended = false)
{
    const auto stts = box("stts", number(0, 4) + number(2, 4)
        + number(2, 4) + number(1000, 4) + number(2, 4) + number(500, 4));
    const auto stss = box("stss", number(0, 4) + number(2, 4) + number(1, 4) + number(3, 4));
    const auto ctts = box("ctts", number(0, 4) + number(1, 4) + number(4, 4) + number(100, 4));
    const auto mdhd = box("mdhd", QByteArray(12, '\0') + number(1000, 4));
    const auto hdlr = box("hdlr", QByteArray(8, '\0') + "vide");
    return box("moov", box("trak", box("mdia", mdhd + hdlr
        + box("minf", box("stbl", stts + stss + ctts)))), extended);
}
QByteArray vint(quint64 value)
{
    for (int width = 1; width <= 8; ++width)
        if (value < (quint64(1) << (7 * width)) - 1)
            return number((quint64(1) << (7 * width)) | value, width);
    return {};
}
QByteArray tag(const char* id, const QByteArray& payload)
{
    return QByteArray::fromHex(id) + vint(payload.size()) + payload;
}
QByteArray uintTag(const char* id, quint64 value)
{
    int width = 1;
    while (width < 8 && value >> (width * 8)) ++width;
    return tag(id, number(value, width));
}
QByteArray track(quint64 number, quint64 type)
{
    return tag("ae", uintTag("d7", number) + uintTag("83", type));
}
QByteArray cue(quint64 track, quint64 time)
{
    return tag("bb", uintTag("b3", time) + tag("b7", uintTag("f7", track) + uintTag("f1", 0)));
}
QByteArray mkv(const QByteArray& entries, const QByteArray& points,
               bool includeCues = true, bool includeInfo = false, bool cuesFirst = false)
{
    QVector<QPair<QByteArray, QByteArray>> parts;
    if (includeInfo) parts.append({QByteArray::fromHex("1549a966"),
        tag("1549a966", uintTag("2ad7b1", 2000000))});
    const auto tracks = qMakePair(QByteArray::fromHex("1654ae6b"), tag("1654ae6b", entries));
    const auto cues = qMakePair(QByteArray::fromHex("1c53bb6b"), tag("1c53bb6b", points));
    if (cuesFirst && includeCues) parts.append(cues);
    parts.append(tracks);
    if (!cuesFirst && includeCues) parts.append(cues);
    auto seekHead = [&](quint64 start) {
        QByteArray seeks;
        for (const auto& part : parts) {
            seeks += tag("4dbb", tag("53ab", part.first) + tag("53ac", number(start, 8)));
            start += part.second.size();
        }
        return tag("114d9b74", seeks);
    };
    QByteArray body = seekHead(seekHead(0).size());
    for (const auto& part : parts) body += part.second;
    return tag("1a45dfa3", {}) + tag("18538067", body);
}
QJsonArray samples(const SamplesResult& result)
{
    return result.tracks.isEmpty() ? QJsonArray{} : result.tracks.begin().value().toObject()["samples"].toArray();
}
class FragmentServer final : public QTcpServer {
public:
    QByteArray bytes;
    QVector<int> chunks;
    QVector<quint64> ranges;
    explicit FragmentServer(QByteArray source, QVector<int> fragments)
        : bytes(std::move(source)), chunks(std::move(fragments))
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto request = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request, handled] {
                    *request += socket->readAll();
                    if (*handled || !request->contains("\r\n\r\n")) return;
                    *handled = true;
                    quint64 offset = 0;
                    for (auto line : request->split('\n')) {
                        line = line.trimmed().toLower();
                        if (line.startsWith("range: bytes=")) offset = line.mid(13).split('-').first().toULongLong();
                    }
                    ranges.append(offset);
                    const auto payload = bytes.mid(qMin(offset, quint64(bytes.size())));
                    socket->write("HTTP/1.1 206 Partial Content\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(payload.size()) + "\r\n\r\n");
                    int position = 0, delay = 10;
                    for (int size : chunks) {
                        const auto part = payload.mid(position, size); position += part.size();
                        QTimer::singleShot(delay, socket, [socket, part] { socket->write(part); socket->flush(); });
                        delay += 10;
                    }
                    const auto tail = payload.mid(position);
                    QTimer::singleShot(delay, socket, [socket, tail] {
                        socket->write(tail); socket->disconnectFromHost();
                    });
                });
            }
        });
    }
    QUrl url() const { return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + "/fixture"); }
};
SamplesResult retrieve(const QUrl& url, bool mp4, bool* timedOut = nullptr)
{
    QEventLoop loop;
    SamplesResult result;
    QPointer<QObject> job;
    bool completed = false;
    auto callback = [&](SamplesResult value) { completed = true; result = std::move(value); loop.quit(); };
    job = mp4 ? retrieveLegacyMp4Samples(url, {}, callback) : retrieveLegacyMkvSamples(url, {}, callback);
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(3000); loop.exec();
    if (job) delete job;
    if (!completed) result.error = "test timeout";
    if (timedOut) *timedOut = !completed;
    return result;
}
void print(const SamplesResult& result)
{
    QJsonArray offsets; for (auto offset : result.sourceOffsets) offsets.append(double(offset));
    std::cout << QJsonDocument(QJsonObject{{"available", result.available}, {"error", result.error},
        {"tracks", result.tracks}, {"sourceOffsets", offsets}, {"bytesFetched", double(result.bytesFetched)}})
        .toJson(QJsonDocument::Compact).constData() << '\n';
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 4 && QString(argv[1]) == "--probe") {
        const QString mode = argv[2], source = argv[3];
        if (mode.startsWith("async-")) print(retrieve(source.startsWith("http") ? QUrl(source) : QUrl::fromLocalFile(source), mode.endsWith("mp4")));
        else { QFile file(source); if (!file.open(QIODevice::ReadOnly)) return 2;
            print(mode == "mp4" ? inspectLegacyMp4Samples(file.readAll()) : inspectLegacyMkvSamples(file.readAll())); }
        return 0;
    }
    int failed = 0, total = 0;
    auto check = [&](const char* name, bool passed, const QString& detail = {}) {
        ++total; if (!passed) ++failed;
        std::cout << (passed ? "PASS " : "FAIL ") << name;
        if (!detail.isEmpty()) std::cout << ": " << detail.toStdString();
        std::cout << '\n';
    };
    const auto mp4 = moov();
    const auto expectedMp4 = inspectLegacyMp4Samples(mp4);
    check("mp4-local-legacy-noncumulative-dts", expectedMp4.available
        && samples(expectedMp4).at(1).toObject()["dts"].toDouble() == 1000
        && samples(expectedMp4).at(1).toObject()["pts"].toDouble() == 1100, expectedMp4.error);
    int headerCase = 0;
    const char* headerNames[] = {"mp4-http-split-8-byte-header", "mp4-http-split-16-byte-header", "mp4-http-split-header-after-ftyp"};
    for (const auto& test : QVector<QPair<QByteArray, QVector<int>>>{
            {mp4, {1, 1, 1, 1, 1, 1, 1}}, {moov(true), {8, 4, 3}},
            {box("ftyp", "isom") + mp4, {14, 1, 1, 1, 1, 1}}}) {
        FragmentServer server(test.first, test.second);
        const bool listening = server.listen(QHostAddress::LocalHost, 0);
        auto actual = listening ? retrieve(server.url(), true) : SamplesResult{};
        check(headerNames[headerCase++], listening && actual.available && actual.tracks == expectedMp4.tracks
            && server.ranges == QVector<quint64>{0}, actual.error);
    }
    const auto hugeOrdinary = number(1, 4) + QByteArray("free") + number(quint64(1) << 40, 8)
        + QByteArray(196608, '\0');
    int limitCase = 0;
    const char* limitNames[] = {"mp4-huge-ordinary-split-64-bit-header", "mp4-huge-ordinary-cumulative-chunks"};
    for (const auto& fragments : QVector<QVector<int>>{{8, 4, 3, 1, 8192, 8192}, {7, 65536, 65536}}) {
        FragmentServer server(hugeOrdinary, fragments);
        const bool listening = server.listen(QHostAddress::LocalHost, 0);
        const auto actual = listening ? retrieve(server.url(), true) : SamplesResult{};
        check(limitNames[limitCase++], listening && !actual.available
            && actual.error == "MP4 atom exceeds resource limit" && actual.bytesFetched <= 65552
            && server.ranges == QVector<quint64>{0}, actual.error);
    }
    const auto video = track(7, 1);
    const auto points = cue(7, 2000) + cue(7, 0) + cue(7, 1000);
    auto infoFree = inspectLegacyMkvSamples(mkv(video, points));
    check("mkv-no-info-required", infoFree.available && samples(infoFree).size() == 3, infoFree.error);
    auto scaled = inspectLegacyMkvSamples(mkv(video, points, true, true));
    check("mkv-raw-cue-timestamps-order", scaled.available && samples(scaled).size() == 3
        && samples(scaled).at(0).toObject()["pts"].toDouble() == 2000
        && samples(scaled).at(1).toObject()["pts"].toDouble() == 0
        && scaled.tracks.begin().value().toObject()["timescale"].toDouble() == 1000, scaled.error);
    for (const auto& test : QVector<QPair<QByteArray, QString>>{
            {mkv(track(1, 2), cue(1, 1)), "no video tracks found"},
            {mkv(video, {}, false), "no cues found in doc -> Segment -> Cues"},
            {mkv(video, {}), "no cues found in doc -> Segment -> Cues"},
            {mkv(video, tag("ec", "x")), "no CuePoints"},
            {mkv(video, tag("bf", QByteArray(4, '\0'))), "no CuePoints"}}) {
        auto actual = inspectLegacyMkvSamples(test.first);
        check("mkv-exact-error", !actual.available && actual.error == test.second, actual.error);
    }
    QByteArray manyTracks;
    for (int i = 0; i < 11; ++i) manyTracks += track(i + 1, i >= 9 ? 1 : 2);
    auto last = inspectLegacyMkvSamples(mkv(manyTracks, cue(10, 90) + cue(11, 100)));
    check("mkv-last-video-document-order-9-vs-10", last.available && last.tracks.size() == 1
        && last.tracks.contains("10") && samples(last).size() == 1
        && samples(last).at(0).toObject()["pts"].toDouble() == 100, last.error);
    auto noMatching = inspectLegacyMkvSamples(mkv(video, cue(8, 100)));
    check("mkv-unmatched-video-cues-empty-success", noMatching.available && samples(noMatching).isEmpty(), noMatching.error);
    auto reversed = inspectLegacyMkvSamples(mkv(video, points, true, false, true));
    check("mkv-cues-before-tracks", reversed.available && reversed.tracks == infoFree.tracks, reversed.error);
    QTemporaryDir directory;
    const auto filename = directory.filePath("legacy-media-index.mkv");
    QFile fixture(filename);
    if (!fixture.open(QIODevice::WriteOnly) || fixture.write(mkv(video, points)) < 0) return 2;
    fixture.close();
    const auto local = retrieve(QUrl::fromLocalFile(filename), false);
    check("mkv-local-async-no-info", local.available && local.tracks == infoFree.tracks, local.error);
    FragmentServer server(mkv(video, points), {1, 2, 3, 4, 5, 6, 7});
    const bool listening = server.listen(QHostAddress::LocalHost, 0);
    const auto remote = listening ? retrieve(server.url(), false) : SamplesResult{};
    check("mkv-http-fragmented-no-info", listening && remote.available && remote.tracks == infoFree.tracks, remote.error);
    const auto mp4Filename = directory.filePath("legacy-media-index.mp4");
    QFile mp4Fixture(mp4Filename);
    if (!mp4Fixture.open(QIODevice::WriteOnly) || mp4Fixture.write(box("free", QByteArray(70000, '\0')) + mp4) < 0) return 2;
    mp4Fixture.close();
    const auto localMp4 = retrieve(QUrl::fromLocalFile(mp4Filename), true);
    check("mp4-local-async-buffered-nonmedia-atom", localMp4.available && localMp4.tracks == expectedMp4.tracks
        && localMp4.sourceOffsets == QVector<quint64>{0}, localMp4.error);
    if (!mp4Fixture.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || mp4Fixture.write(box("ftyp", QByteArray(24, '\0')) + box("mdat", QByteArray(100, '\0'))
            + box("free", QByteArray(70000, '\0')) + mp4) < 0) return 2;
    mp4Fixture.close();
    const auto postMdat = retrieve(QUrl::fromLocalFile(mp4Filename), true);
    check("mp4-local-async-post-mdat-partial-free-restart", postMdat.available && postMdat.tracks == expectedMp4.tracks
        && postMdat.sourceOffsets == QVector<quint64>{0, 140}, postMdat.error);
    for (bool isMp4 : {true, false}) {
        FragmentServer cancelServer(isMp4 ? mp4 : mkv(video, points), {1, 1, 1});
        const bool bound = cancelServer.listen(QHostAddress::LocalHost, 0);
        bool called = false;
        auto callback = [&](SamplesResult) { called = true; };
        QObject* job = isMp4 ? retrieveLegacyMp4Samples(cancelServer.url(), {}, callback)
                             : retrieveLegacyMkvSamples(cancelServer.url(), {}, callback);
        QEventLoop loop;
        QTimer::singleShot(5, &loop, [job] { delete job; });
        QTimer::singleShot(80, &loop, &QEventLoop::quit);
        loop.exec();
        check(isMp4 ? "mp4-cancel-suppresses-callback" : "mkv-cancel-suppresses-callback", bound && !called);
    }
    std::cout << total - failed << '/' << total << " legacy media index tests passed\n";
    return failed ? 1 : 0;
}
