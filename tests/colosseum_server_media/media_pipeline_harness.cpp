#include "colosseum_server/media/MediaPipeline.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>
#include <cstdlib>

using namespace ColosseumServer::Media;

namespace {

[[noreturn]] void fail(const QString &message)
{
    std::fprintf(stderr, "FAIL: %s\n", message.toUtf8().constData());
    std::exit(1);
}

void require(bool condition, const QString &message)
{
    if (!condition)
        fail(message);
}
QString fixture(const QString &name)
{
    return QString::fromUtf8(MEDIA_FIXTURES_DIR) + QLatin1Char('/') + name;
}

void testSubtitleRendering()
{
    QFile source(fixture(QStringLiteral("subtitle.srt")));
    require(source.open(QIODevice::ReadOnly), QStringLiteral("subtitle fixture missing"));
    QString error;
    const auto cues = SubtitleService::parseSrt(source.readAll(), &error);
    require(error.isEmpty(), QStringLiteral("subtitle parse failed: %1").arg(error));
    require(cues.size() == 1, QStringLiteral("expected one subtitle cue"));
    require(cues.at(0).number == 0 && cues.at(0).startMs == 0 && cues.at(0).endMs == 750,
            QStringLiteral("subtitle timing/number drift"));
    require(cues.at(0).text == QStringLiteral("Aqueduct oracle\n"),
            QStringLiteral("subtitle text drift"));

    const QByteArray srt = SubtitleService::render(cues, SubtitleFormat::Srt, 0);
    require(srt == "0\n00:00:00,000 --> 00:00:00,750\nAqueduct oracle\n\n\n",
            QStringLiteral("SRT render does not match oracle"));
    const QByteArray vtt = SubtitleService::render(cues, SubtitleFormat::Vtt, 250);
    require(vtt == "WEBVTT\n\n0\n00:00:00.250 --> 00:00:01.000\nAqueduct oracle\n\n\n",
            QStringLiteral("VTT offset render drift"));
}
void testOpenSubtitlesHash()
{
    QTemporaryDir temp;
    require(temp.isValid(), QStringLiteral("temp dir failed"));
    const QString path = temp.filePath(QStringLiteral("hash.bin"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), QStringLiteral("hash fixture open failed"));
    QByteArray bytes(200000, Qt::Uninitialized);
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(i % 251);
    require(file.write(bytes) == bytes.size(), QStringLiteral("hash fixture write failed"));
    file.close();

    QString error;
    const QString hash = SubtitleService::openSubHashFile(path, &error);
    require(error.isEmpty(), QStringLiteral("opensub hash failed: %1").arg(error));
    require(hash == QStringLiteral("e19d5212c9812cd6"),
            QStringLiteral("opensub hash drift: %1").arg(hash));
}

void testEmbeddedSamples()
{
    const QStringList keys = EmbeddedSamples::keys();
    require(keys == QStringList({QStringLiteral("aac-6chan"), QStringLiteral("ac3-2chan"),
                                 QStringLiteral("hevc")}),
            QStringLiteral("embedded sample key set drift"));
    const EmbeddedSample wav = EmbeddedSamples::get(QStringLiteral("aac-6chan"));
    require(wav.container == QStringLiteral("wav") && wav.mime == QStringLiteral("audio/x-wav"),
            QStringLiteral("AAC sample metadata drift"));
    require(wav.bytes.startsWith("RIFF"), QStringLiteral("AAC sample bytes drift"));

    const EmbeddedSample hevc = EmbeddedSamples::get(QStringLiteral("hevc"));
    require(hevc.container == QStringLiteral("mkv")
                && hevc.mime == QStringLiteral("video/x-matroska"),
            QStringLiteral("HEVC sample metadata drift"));
    require(hevc.bytes.size() > 4
                && static_cast<unsigned char>(hevc.bytes[0]) == 0x1a
                && static_cast<unsigned char>(hevc.bytes[1]) == 0x45
                && static_cast<unsigned char>(hevc.bytes[2]) == 0xdf
                && static_cast<unsigned char>(hevc.bytes[3]) == 0xa3,
            QStringLiteral("HEVC sample is not Matroska"));
}

void testTrackParser()
{
    QString error;
    const QVector<TrackInfo> tracks = TrackParser::parseFile(
        fixture(QStringLiteral("sample.mp4")), 25 * 1024 * 1024, &error);
    require(error.isEmpty(), QStringLiteral("track parse failed: %1").arg(error));
    require(tracks.size() == 2, QStringLiteral("expected video+audio tracks"));
    require(tracks[0].id == 1 && tracks[0].type == QStringLiteral("video")
                && tracks[0].codec == QStringLiteral("AVC1"),
            QStringLiteral("video track metadata drift"));
    require(tracks[0].language.isNull() && tracks[0].label.isNull(),
            QStringLiteral("generic video language/label should normalize to null"));
    require(tracks[1].id == 2 && tracks[1].type == QStringLiteral("audio")
                && tracks[1].codec == QStringLiteral("MP4A"),
            QStringLiteral("audio track metadata drift"));
    require(tracks[1].language.isNull() && tracks[1].label.isNull(),
            QStringLiteral("generic audio language/label should normalize to null"));
}

QByteArray ebmlSize(qsizetype size)
{
    if (size < 0x7f)
        return QByteArray(1, char(0x80 | int(size)));
    if (size < 0x3fff) {
        QByteArray out;
        out.append(char(0x40 | ((size >> 8) & 0x3f)));
        out.append(char(size & 0xff));
        return out;
    }
    fail(QStringLiteral("test EBML element too large"));
}

QByteArray ebmlElement(const char *idHex, const QByteArray &data)
{
    return QByteArray::fromHex(idHex) + ebmlSize(data.size()) + data;
}

QByteArray matroskaTrack(int number, int type, const QByteArray &language,
                         const QByteArray &label, const QByteArray &codec)
{
    QByteArray data;
    data += ebmlElement("D7", QByteArray(1, char(number)));
    data += ebmlElement("83", QByteArray(1, char(type)));
    data += ebmlElement("22B59C", language);
    data += ebmlElement("536E", label);
    data += ebmlElement("86", codec);
    return ebmlElement("AE", data);
}

V2ProbeResult syntheticV2Probe()
{
    V2ProbeResult result;
    result.format.name = QStringLiteral("mov,mp4,m4a,3gp,3g2,mj2");
    result.format.duration = 1.0;
    V2Stream video;
    video.id = 0;
    video.index = 0;
    video.track = QStringLiteral("video");
    video.codec = QStringLiteral("h264");
    result.streams.append(video);
    V2Stream audio;
    audio.id = 0;
    audio.index = 1;
    audio.track = QStringLiteral("audio");
    audio.codec = QStringLiteral("aac");
    audio.language = QStringLiteral("und");
    audio.channels = 2;
    audio.sampleRate = 48000;
    result.streams.append(audio);
    return result;
}

void testMatroskaStructuredTracks()
{
    QByteArray tracksData;
    tracksData += matroskaTrack(5, 1, "eng", "Main", "V_MPEGH/ISO/HEVC");
    tracksData += matroskaTrack(9, 1, "jpn", "Alt", "V_MPEGH/ISO/HEVC");
    const QByteArray bytes = ebmlElement("1A45DFA3", {})
        + ebmlElement("18538067", ebmlElement("1654AE6B", tracksData));

    QString error;
    const QVector<TrackInfo> tracks = TrackParser::parseBytes(bytes, &error);
    require(error.isEmpty(), QStringLiteral("structured Matroska parse failed: %1").arg(error));
    require(tracks.size() == 2, QStringLiteral("duplicate-codec TrackEntry was collapsed"));
    require(tracks[0].id == 5 && tracks[0].type == QStringLiteral("video")
                && tracks[0].language == QStringLiteral("eng")
                && tracks[0].label == QStringLiteral("Main")
                && tracks[0].codec == QStringLiteral("MPEGH/ISO/HEVC"),
            QStringLiteral("first Matroska TrackEntry metadata drift"));
    require(tracks[1].id == 9 && tracks[1].language == QStringLiteral("jpn")
                && tracks[1].label == QStringLiteral("Alt"),
            QStringLiteral("second Matroska TrackEntry metadata drift"));
}

void testMatroskaTrackParser()
{
    const EmbeddedSample sample = EmbeddedSamples::get(QStringLiteral("hevc"));
    QString error;
    const QVector<TrackInfo> tracks = TrackParser::parseBytes(sample.bytes, &error);
    require(error.isEmpty(), QStringLiteral("Matroska track parse failed: %1").arg(error));
    require(tracks.size() == 1, QStringLiteral("expected one Matroska video track"));
    require(tracks[0].id == 1 && tracks[0].type == QStringLiteral("video")
                && tracks[0].codec == QStringLiteral("MPEGH/ISO/HEVC"),
            QStringLiteral("Matroska video metadata drift"));
}

void testHlsV2MasterPlaylist()
{
    const QByteArray query =
        "mediaURL=http%3A%2F%2F127.0.0.1%3A11580%2Fsample.mp4&maxAudioChannels=2";
    const QByteArray playlist = HlsV2Playlist::master(syntheticV2Probe(), query);
    const QByteArray expected =
        "#EXTM3U\n"
        "#EXT-X-VERSION:7\n"
        "#EXT-X-MEDIA:TYPE=VIDEO,GROUP-ID=\"video\",NAME=\"Video\",AUTOSELECT=YES,DEFAULT=YES\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"und\",LANGUAGE=\"und\",AUTOSELECT=YES,DEFAULT=YES,URI=\"audio0.m3u8?"
        + query + "\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=164000,VIDEO=\"video\",AUDIO=\"audio\",NAME=\"Main\"\n"
        "video0.m3u8?" + query;
    require(playlist == expected,
            QStringLiteral("HLSv2 master playlist does not match 4.20.17 oracle"));
    require(playlist.size() == 448, QStringLiteral("oracle HLS master length drift"));
}

void testLegacyPlaylistMath()
{
    LegacyProbeResult probe;
    probe.durationMs = 12500;
    probe.bitrate = 1500000;
    LegacyStream video;
    video.codecType = QStringLiteral("video");
    video.codecName = QStringLiteral("h264");
    video.width = 1920;
    video.height = 1080;
    video.stream = 0;
    video.fps = 24.0;
    probe.streams.append(video);
    LegacyStream audio;
    audio.codecType = QStringLiteral("audio");
    audio.codecName = QStringLiteral("aac");
    audio.stream = 1;
    probe.streams.append(audio);

    const LegacyPrepared prepared = LegacyHls::prepare(probe);
    require(prepared.qualities == QVector<int>({320, 480, 720}),
            QStringLiteral("legacy quality ladder drift"));
    require(prepared.uniformSegments.size() == 3,
            QStringLiteral("12.5s media should make three uniform segments"));
    require(prepared.uniformSegments[0].durationMs == 6000
                && prepared.uniformSegments[1].durationMs == 6000
                && prepared.uniformSegments[2].durationMs == 500,
            QStringLiteral("legacy six-second segmentation drift"));

    const QByteArray playlist = LegacyHls::streamPlaylist(prepared, 320, QString(), QByteArray());
    require(playlist.startsWith("#EXTM3U\n#EXT-X-VERSION:4\n#EXT-X-TARGETDURATION:6\n"),
            QStringLiteral("legacy stream playlist header drift"));
    require(playlist.contains("#EXTINF:6.000,\n0.ts\n")
                && playlist.contains("#EXTINF:0.500,\n2.ts\n")
                && playlist.endsWith("#EXT-X-ENDLIST\n"),
            QStringLiteral("legacy stream segment lines drift"));
}

void testRegistryLifecycle()
{
    Executables tools;
    HlsV2Registry registry(tools, 2, 120000);
    HlsV2Options a;
    a.mediaUrl = QStringLiteral("file:///a.mp4");
    HlsV2Options b = a;
    b.mediaUrl = QStringLiteral("file:///b.mp4");
    HlsV2Options c = a;
    c.mediaUrl = QStringLiteral("file:///c.mp4");

    registry.acquire(QStringLiteral("a"), a, 1000);
    registry.acquire(QStringLiteral("b"), b, 2000);
    registry.acquire(QStringLiteral("a"), a, 3000);
    registry.acquire(QStringLiteral("c"), c, 4000);
    require(registry.contains(QStringLiteral("a")) && registry.contains(QStringLiteral("c")),
            QStringLiteral("newest converters should survive concurrency eviction"));
    require(!registry.contains(QStringLiteral("b")),
            QStringLiteral("oldest converter was not evicted"));

    registry.expireInactive(124000);
    require(!registry.contains(QStringLiteral("a")) && registry.contains(QStringLiteral("c")),
            QStringLiteral("120s inactivity expiry drift"));
    registry.destroy(QStringLiteral("c"));
    require(registry.contains(QStringLiteral("c")) && registry.isDestroyed(QStringLiteral("c")),
            QStringLiteral("destroy route must retain the registry entry like 4.20.17"));
}

bool processToolsAvailable(const Executables &tools)
{
    return !tools.ffmpeg.isEmpty() && !tools.ffprobe.isEmpty();
}

void testProcessTimeoutCleanup(const Executables &tools)
{
    if (tools.ffmpeg.isEmpty()) return;
    QElapsedTimer timer;
    timer.start();
    const ProcessResult timed = MediaProcess::run(
        tools.ffmpeg,
        {QStringLiteral("-hide_banner"), QStringLiteral("-f"), QStringLiteral("lavfi"),
         QStringLiteral("-i"), QStringLiteral("testsrc=size=16x16:rate=1"),
         QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")},
        100);
    require(timed.timedOut, QStringLiteral("managed ffmpeg timeout was not reported"));
    require(timer.elapsed() < 5000, QStringLiteral("timed-out ffmpeg was not cleaned up promptly"));
}

void testRealProbeAndSegments(const Executables &tools)
{
    if (!processToolsAvailable(tools)) {
        std::printf("MEDIA_PROCESS_TESTS_SKIPPED\n");
        return;
    }

    MediaProbe probe(tools);
    LegacyProbeResult legacy;
    QString error;
    require(probe.legacyProbe(fixture(QStringLiteral("sample.mp4")), &legacy, &error, 10000),
            QStringLiteral("legacy ffmpeg probe failed: %1").arg(error));
    require(legacy.container == QStringLiteral("mp4") && legacy.durationMs == 1000
                && legacy.bitrate == 24000 && legacy.streams.size() == 2,
            QStringLiteral("legacy probe oracle core fields drift"));
    require(legacy.streams[0].codecType == QStringLiteral("video")
                && legacy.streams[0].codecName == QStringLiteral("h264")
                && legacy.streams[0].fps == 24.0 && legacy.streams[0].language == QStringLiteral("und"),
            QStringLiteral("legacy video stream drift"));
    require(legacy.streams[0].width < 0 && legacy.streams[0].height < 0,
            QStringLiteral("160x90 fixture must preserve oracle's null-size regex quirk"));

    V2ProbeResult v2;
    require(probe.probeV2(fixture(QStringLiteral("sample.mp4")), &v2, &error, 10000),
            QStringLiteral("ffprobe v2 probe failed: %1").arg(error));
    require(qAbs(v2.format.duration - 1.003) < 0.0001 && v2.streams.size() == 2,
            QStringLiteral("v2 probe format/stream count drift"));

    HlsV2Options options;
    options.mediaUrl = fixture(QStringLiteral("sample.mp4"));
    options.maxAudioChannels = 2;
    HlsV2Converter converter(tools, QStringLiteral("fixture"), options);
    const QByteArray master = converter.playlist(QStringLiteral("master"), &error);
    require(error.isEmpty() && master.startsWith("#EXTM3U\n#EXT-X-VERSION:7\n"),
            QStringLiteral("real HLSv2 master failed: %1").arg(error));
    const QByteArray media = converter.playlist(QStringLiteral("video0"), &error);
    require(error.isEmpty() && media.contains("#EXT-X-MAP:URI=\"video0/init.mp4?"),
            QStringLiteral("real HLSv2 media playlist failed: %1").arg(error));
    const QByteArray init = converter.initSegment(QStringLiteral("video0"), &error);
    require(error.isEmpty() && init.contains("ftyp") && init.contains("moov"),
            QStringLiteral("HLSv2 init segment failed: %1").arg(error));
    const QByteArray segment = converter.mediaSegment(QStringLiteral("video0"), 1, &error);
    require(error.isEmpty() && segment.contains("moof") && segment.contains("mdat"),
            QStringLiteral("HLSv2 media segment failed: %1").arg(error));

    const ProcessResult version = MediaProcess::run(tools.ffprobe, {QStringLiteral("-version")}, 5000);
    require(!version.timedOut && version.exitCode == 0,
            QStringLiteral("managed ffprobe lifecycle failed: %1").arg(version.error));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testSubtitleRendering();
    testOpenSubtitlesHash();
    testEmbeddedSamples();
    testTrackParser();
    testMatroskaStructuredTracks();
    testMatroskaTrackParser();
    testHlsV2MasterPlaylist();
    testLegacyPlaylistMath();
    testRegistryLifecycle();

    const Executables tools = ExecutableLocator::locateAll();
    testProcessTimeoutCleanup(tools);
    testRealProbeAndSegments(tools);
    std::printf("COLOSSEUM_SERVER_MEDIA_OK\n");
    return 0;
}
