#include "player2/core/DemuxSession.h"
#include "player2/core/PlaybackGeneration.h"
#include "player2/core/Player2Session.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

PlaybackRequest requestFor(const QString &path)
{
    PlaybackRequest request;
    request.source = QUrl::fromLocalFile(path);
    request.mediaId = QFileInfo(path).baseName();
    request.title = request.mediaId;
    return request;
}

DemuxMetadata inspect(const QString &path, DemuxEndReason *endReason = nullptr,
                      int *packets = nullptr)
{
    DemuxSession session;
    DemuxMetadata metadata;
    DemuxEndReason reason = DemuxEndReason::Failed;
    int packetCount = 0;
    bool opened = false;
    bool ended = false;
    QEventLoop loop;
    QObject::connect(&session, &DemuxSession::opened, &loop,
                     [&](quint64 generation, const DemuxMetadata &value) {
        require(generation == 1, "opened signal carried the wrong generation");
        metadata = value;
        opened = true;
    });
    QObject::connect(&session, &DemuxSession::packetObserved, &loop,
                     [&](quint64 generation, const DemuxPacketInfo &) {
        require(generation == 1, "packet signal carried the wrong generation");
        ++packetCount;
    });
    QObject::connect(&session, &DemuxSession::ended, &loop,
                     [&](quint64 generation, DemuxEndReason value, const Player2Error &) {
        require(generation == 1, "ended signal carried the wrong generation");
        reason = value;
        ended = true;
        loop.quit();
    });
    QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
    session.open(requestFor(path), 1);
    loop.exec();
    require(opened, "fixture did not publish metadata");
    require(ended, "fixture demux timed out");
    require(reason == DemuxEndReason::EndOfFile, "fixture did not end at EOF");
    require(packetCount > 0, "fixture emitted no packets");
    if (endReason)
        *endReason = reason;
    if (packets)
        *packets = packetCount;
    return metadata;
}

int streamCount(const DemuxMetadata &metadata, const QString &type)
{
    int count = 0;
    for (const DemuxStreamInfo &stream : metadata.streams) {
        if (stream.type == type)
            ++count;
    }
    return count;
}

void generationIsMonotonic()
{
    PlaybackGeneration generation;
    require(generation.current() == 0, "generation must start at zero");
    require(generation.advance() == 1 && generation.accepts(1), "first advance failed");
    require(generation.advance() == 2 && !generation.accepts(1) && generation.accepts(2),
            "stale generation was accepted");
}

void fixtureDiscoveryIsTyped(const QDir &fixtures)
{
    const auto video = inspect(fixtures.filePath(QStringLiteral("video-only.mp4")));
    require(streamCount(video, QStringLiteral("video")) == 1, "video-only video count");
    require(streamCount(video, QStringLiteral("audio")) == 0, "video-only audio count");

    const auto av = inspect(fixtures.filePath(QStringLiteral("av.mkv")));
    require(av.durationUs >= 1'900'000 && av.durationUs <= 2'100'000,
            "A/V duration is outside fixture bounds");
    require(streamCount(av, QStringLiteral("video")) == 1, "A/V video count");
    require(streamCount(av, QStringLiteral("audio")) == 1, "A/V audio count");
    require(av.tags.value(QStringLiteral("title")).toString() ==
                QStringLiteral("Player 2 A/V fixture"),
            "container title metadata was not preserved");

    const auto twoAudio = inspect(fixtures.filePath(QStringLiteral("two-audio.mkv")));
    require(streamCount(twoAudio, QStringLiteral("audio")) == 2,
            "two-audio fixture did not expose two tracks");

    const auto subtitle = inspect(fixtures.filePath(QStringLiteral("embedded-subtitle.mkv")));
    require(streamCount(subtitle, QStringLiteral("subtitle")) == 1,
            "embedded subtitle was not discovered");

    const auto chaptered = inspect(fixtures.filePath(QStringLiteral("chaptered.mkv")));
    require(chaptered.chapterCount == 2, "chapter metadata was not discovered");
}

void cancellationDuringOpenIsBounded(const QString &path)
{
    DemuxSession session;
    DemuxEndReason reason = DemuxEndReason::Failed;
    bool ended = false;
    QEventLoop loop;
    QObject::connect(&session, &DemuxSession::ended, &loop,
                     [&](quint64 generation, DemuxEndReason value, const Player2Error &) {
        if (generation == 77) {
            reason = value;
            ended = true;
            loop.quit();
        }
    });
    session.open(requestFor(path), 77);
    session.cancel();
    QTimer::singleShot(2'000, &loop, &QEventLoop::quit);
    loop.exec();
    require(ended, "cancelled open did not emit an end reason");
    require(reason == DemuxEndReason::Cancelled, "cancelled open reported the wrong reason");
    require(!session.running(), "cancelled worker remained running");
}

void closeReopenRejectsOldPackets(const QDir &fixtures)
{
    Player2Session session;
    bool reopened = false;
    bool sawPlaying = false;
    bool endedCurrent = false;
    bool acceptedStalePacket = false;
    quint64 reopenedGeneration = 0;
    QEventLoop loop;
    QObject::connect(&session, &Player2Session::stateChanged, &loop, [&] {
        if (session.state() == Player2State::Playing)
            sawPlaying = true;
    });
    QObject::connect(&session, &Player2Session::packetAccepted, &loop,
                     [&](quint64 generation, const DemuxPacketInfo &) {
        if (reopened && generation != reopenedGeneration)
            acceptedStalePacket = true;
        if (!reopened) {
            reopened = true;
            session.open(requestFor(fixtures.filePath(QStringLiteral("video-only.mp4"))));
            reopenedGeneration = session.generation();
        }
    });
    QObject::connect(&session, &Player2Session::demuxEnded, &loop,
                     [&](DemuxEndReason reason) {
        if (reopened && reason == DemuxEndReason::EndOfFile) {
            endedCurrent = true;
            loop.quit();
        }
    });
    QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
    session.open(requestFor(fixtures.filePath(QStringLiteral("av.mkv"))));
    loop.exec();
    require(reopened && endedCurrent, "close/reopen sequence did not complete");
    require(sawPlaying, "session never reached Playing");
    require(!acceptedStalePacket, "old-generation packet escaped after reopen");
    require(session.duration() > 1.5, "reopened duration was not populated");
    require(session.tracks().size() == 1, "reopened track list was not replaced");
    require(session.state() == Player2State::Ended, "EOF did not transition to Ended");
    const quint64 beforeClose = session.generation();
    session.close();
    require(session.state() == Player2State::Idle, "close did not return to Idle");
    require(session.generation() > beforeClose, "close did not advance generation");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    try {
        require(argc == 2, "usage: player2_demux_session_test FIXTURE_DIRECTORY");
        const QDir fixtures(QString::fromLocal8Bit(argv[1]));
        require(fixtures.exists(), "fixture directory does not exist");
        generationIsMonotonic();
        fixtureDiscoveryIsTyped(fixtures);
        cancellationDuringOpenIsBounded(fixtures.filePath(QStringLiteral("av.mkv")));
        closeReopenRejectsOldPackets(fixtures);
    } catch (const std::exception &error) {
        std::cerr << "player2_demux_session_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_demux_session_test: PASS\n";
    return EXIT_SUCCESS;
}
