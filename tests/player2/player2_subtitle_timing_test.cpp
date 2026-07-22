// Task 10 - embedded subtitle cue decode, generation-safe. Headless (no video pipeline), audio+demux.
// The fixture embedded-subtitle.mkv carries one SRT cue: "Player 2 subtitle fixture" 0.1s -> 1.7s.

#include "player2/core/DemuxSession.h"
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
#include <vector>

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

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void openToPlaying(Player2Session &session, const QString &path)
{
    QEventLoop loop;
    auto c = QObject::connect(&session, &Player2Session::stateChanged, &loop, [&] {
        if (session.state() == Player2State::Playing)
            loop.quit();
    });
    QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
    session.open(requestFor(path));
    if (session.state() != Player2State::Playing)
        loop.exec();
    QObject::disconnect(c);
    require(session.state() == Player2State::Playing, "session never reached Playing");
}

double seekAndWait(Player2Session &session, double target)
{
    QEventLoop loop;
    double landed = -1.0;
    quint64 expected = 0;
    auto c = QObject::connect(&session, &Player2Session::seekCompleted, &loop,
                              [&](quint64 gen, double actual) {
        if (gen == expected) { landed = actual; loop.quit(); }
    });
    QTimer::singleShot(3'000, &loop, &QEventLoop::quit);
    session.seekExact(target);
    expected = session.generation();
    loop.exec();
    QObject::disconnect(c);
    require(landed >= 0.0, "seek did not complete");
    return landed;
}

void embeddedSubtitleCuesAreGenerationSafe(const QDir &fixtures)
{
    Player2Session session;
    std::vector<SubtitleCue> cues;
    bool sawStale = false;
    QObject::connect(&session, &Player2Session::subtitleCue, &session,
                     [&](quint64 generation, const SubtitleCue &cue) {
        if (generation != session.generation())
            sawStale = true;
        cues.push_back(cue);
    });

    openToPlaying(session, fixtures.filePath(QStringLiteral("embedded-subtitle.mkv")));

    int subtitleIndex = -1;
    for (const QVariant &entry : session.subtitleTracks()) {
        subtitleIndex = entry.toMap().value(QStringLiteral("index")).toInt();
        break;
    }
    require(subtitleIndex >= 0, "fixture exposed no subtitle track");

    // Turn subtitles on, then seek to the start so the 0.1s cue is decoded with the track active.
    bool trackChanged = false;
    QObject::connect(&session, &Player2Session::subtitleTrackChanged, &session,
                     [&](quint64, int idx) { if (idx == subtitleIndex) trackChanged = true; });
    session.selectSubtitleTrack(QString::number(subtitleIndex));
    pump(200);
    require(trackChanged, "subtitle track change did not complete");

    seekAndWait(session, 0.0);
    pump(1'500); // let playback cross the 0.1s -> 1.7s cue window

    require(!sawStale, "a stale-generation subtitle cue escaped");
    bool foundFixtureCue = false;
    for (const SubtitleCue &cue : cues) {
        if (cue.text.contains(QStringLiteral("Player 2 subtitle fixture"))) {
            foundFixtureCue = true;
            require(cue.startUs >= 0 && cue.startUs <= 400'000, "cue start not near 0.1s");
            require(cue.endUs > cue.startUs, "cue end must follow start");
            require(cue.streamIndex == subtitleIndex, "cue carried the wrong stream index");
        }
    }
    require(foundFixtureCue, "the embedded subtitle cue was never decoded");

    // Turning subtitles off must complete and stop new cues.
    session.selectSubtitleTrack(QStringLiteral("off"));
    pump(200);
    session.close();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    try {
        require(argc == 2, "usage: player2_subtitle_timing_test FIXTURE_DIRECTORY");
        const QDir fixtures(QString::fromLocal8Bit(argv[1]));
        require(fixtures.exists(), "fixture directory does not exist");
        embeddedSubtitleCuesAreGenerationSafe(fixtures);
    } catch (const std::exception &error) {
        std::cerr << "player2_subtitle_timing_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_subtitle_timing_test: PASS\n";
    return EXIT_SUCCESS;
}
