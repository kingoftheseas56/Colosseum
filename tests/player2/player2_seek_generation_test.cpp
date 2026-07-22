// Task 8 — generation-safe seek, flush, EOS, real pause, frame step and audio-track switching.
//
// Runs headless (QCoreApplication, no video pipeline) exactly like the demux test: the demux
// worker skips the D3D11 branch when no pipeline is attached, so this exercises the seek command
// channel, the single-generation barrier, audio flush and real pause without a GPU. The real GPU
// A/V path is proven separately by the Wire soak.
//
// The load-bearing invariant: after any seek/track/frame-step advances the PlaybackGeneration, no
// product tagged with an older generation may ever reach an observer. Player2Session only emits
// packetAccepted(gen, ...) when gen == its current generation, so asserting that equality on every
// delivery is a direct test that no stale product escaped the barrier.

#include "player2/core/DemuxSession.h"
#include "player2/core/Player2Session.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>

#include <cmath>
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

// Guards every delivery for the lifetime of a session: any packet whose generation differs from the
// session's current generation is a stale product that escaped the barrier.
struct StaleGuard
{
    Player2Session &session;
    bool sawStale = false;
    quint64 lastGeneration = 0;
    explicit StaleGuard(Player2Session &s)
        : session(s)
    {
        QObject::connect(&session, &Player2Session::packetAccepted, &session,
                         [this](quint64 generation, const DemuxPacketInfo &) {
            if (generation != session.generation())
                sawStale = true;
            lastGeneration = generation;
        });
    }
};

// Drives the session to Playing on the given fixture within a bounded wait.
void openToPlaying(Player2Session &session, const QString &path, int timeoutMs = 5'000)
{
    QEventLoop loop;
    bool playing = false;
    auto connection = QObject::connect(&session, &Player2Session::stateChanged, &loop, [&] {
        if (session.state() == Player2State::Playing) {
            playing = true;
            loop.quit();
        }
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    session.open(requestFor(path));
    if (session.state() != Player2State::Playing)
        loop.exec();
    QObject::disconnect(connection);
    require(playing || session.state() == Player2State::Playing, "session never reached Playing");
}

// Issues one seek and waits for its matching completion. Returns the landed position.
double seekAndWait(Player2Session &session, double target, int timeoutMs = 3'000)
{
    QEventLoop loop;
    bool done = false;
    double landed = -1.0;
    quint64 expected = 0;
    auto connection = QObject::connect(&session, &Player2Session::seekCompleted, &loop,
                                       [&](quint64 generation, double actualSeconds) {
        if (generation == expected) {
            landed = actualSeconds;
            done = true;
            loop.quit();
        }
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    session.seekExact(target);
    expected = session.generation();
    loop.exec();
    QObject::disconnect(connection);
    require(done, "seek did not complete within its deadline");
    return landed;
}

// 100 deterministic seeks across the whole fixture; every completion must carry the current
// generation and no stale packet may be observed.
void hundredSeeksAreGenerationSafe(const QDir &fixtures)
{
    Player2Session session;
    StaleGuard guard(session);
    openToPlaying(session, fixtures.filePath(QStringLiteral("av.mkv")));
    const double duration = session.duration();
    require(duration > 1.5, "A/V fixture duration was not populated");

    QElapsedTimer wall;
    wall.start();
    int completed = 0;
    // Deterministic target walk covering start, end and interior without Math.random.
    const double points[] = {0.10, 1.80, 0.50, 1.20, 0.00, 1.90, 0.80, 0.30, 1.50, 0.05};
    for (int i = 0; i < 100; ++i) {
        const double target = points[i % 10] * (duration / 2.0);
        const double landed = seekAndWait(session, target);
        require(landed >= -0.001 && landed <= duration + 0.25, "seek landed outside the media");
        // A seek resolves out of Seeking; a target near the very end may legitimately reach EOF
        // (headless there is no frame pacing, so the short tail plays out at once).
        require(session.state() == Player2State::Playing || session.state() == Player2State::Ended,
                "seek did not resolve to Playing/Ended (i=" + std::to_string(i) + " state=" +
                    std::to_string(static_cast<int>(session.state())) + ")");
        require(!guard.sawStale, "a stale-generation packet escaped after a seek");
        ++completed;
        require(wall.elapsed() < 55'000, "100-seek walk exceeded its time budget");
    }
    require(completed == 100, "did not observe 100 seek completions");
    session.close();
}

void seekNearBoundsIsExact(const QDir &fixtures)
{
    Player2Session session;
    StaleGuard guard(session);
    openToPlaying(session, fixtures.filePath(QStringLiteral("av.mkv")));
    const double duration = session.duration();

    const double atStart = seekAndWait(session, 0.0);
    require(atStart <= 0.20, "seek to start did not land near zero");
    const double nearEnd = seekAndWait(session, duration - 0.05);
    require(nearEnd <= duration + 0.10, "seek near end overshot the media");
    require(!guard.sawStale, "boundary seek leaked a stale packet");
    session.close();
}

// Real pause must suspend decode: the position stops advancing while paused and resumes after.
void realPauseSuspendsProgress(const QDir &fixtures)
{
    Player2Session session;
    openToPlaying(session, fixtures.filePath(QStringLiteral("av.mkv")));
    seekAndWait(session, 0.0);

    session.pause();
    require(session.state() == Player2State::Paused, "pause did not enter Paused");
    // Let any in-flight decode settle, then confirm the position is frozen.
    QEventLoop settle;
    QTimer::singleShot(400, &settle, &QEventLoop::quit);
    settle.exec();
    const double frozen = session.position();
    QEventLoop hold;
    QTimer::singleShot(500, &hold, &QEventLoop::quit);
    hold.exec();
    require(std::abs(session.position() - frozen) < 0.05,
            "position advanced while paused — decode was not suspended");

    session.play();
    require(session.state() == Player2State::Playing, "resume did not re-enter Playing");
    QEventLoop resumeLoop;
    bool advanced = false;
    auto connection = QObject::connect(&session, &Player2Session::positionChanged, &resumeLoop, [&] {
        if (session.position() > frozen + 0.02) {
            advanced = true;
            resumeLoop.quit();
        }
    });
    QTimer::singleShot(2'000, &resumeLoop, &QEventLoop::quit);
    resumeLoop.exec();
    QObject::disconnect(connection);
    require(advanced, "position did not resume advancing after play");
    session.close();
}

// Frame step on a paused fixture moves forward and back without leaving Paused or leaking staleness.
void frameStepOnPausedFixture(const QDir &fixtures)
{
    Player2Session session;
    StaleGuard guard(session);
    openToPlaying(session, fixtures.filePath(QStringLiteral("av.mkv")));
    seekAndWait(session, session.duration() / 2.0);
    session.pause();
    require(session.state() == Player2State::Paused, "frame-step setup did not pause");

    QEventLoop settle;
    QTimer::singleShot(300, &settle, &QEventLoop::quit);
    settle.exec();
    const double base = session.position();

    // frameStep publishes a seekCompleted like any precise reposition.
    auto stepAndWait = [&](int frames) -> double {
        QEventLoop loop;
        double landed = -1.0;
        quint64 expected = 0;
        auto c = QObject::connect(&session, &Player2Session::seekCompleted, &loop,
                                  [&](quint64 generation, double actualSeconds) {
            if (generation == expected) { landed = actualSeconds; loop.quit(); }
        });
        QTimer::singleShot(3'000, &loop, &QEventLoop::quit);
        session.frameStep(frames);
        expected = session.generation();
        loop.exec();
        QObject::disconnect(c);
        require(landed >= 0.0, "frame step did not complete");
        return landed;
    };

    const double forward = stepAndWait(1);
    require(forward >= base - 0.001, "forward frame step moved backward");
    require(session.state() == Player2State::Paused, "forward frame step left Paused");
    const double backward = stepAndWait(-1);
    require(backward <= forward + 0.001, "backward frame step moved forward");
    require(session.state() == Player2State::Paused, "backward frame step left Paused");
    require(!guard.sawStale, "frame step leaked a stale packet");
    session.close();
}

// Audio track switching during play, paused and immediately after a seek is generation-safe.
void audioTrackSwitchIsGenerationSafe(const QDir &fixtures)
{
    Player2Session session;
    StaleGuard guard(session);
    openToPlaying(session, fixtures.filePath(QStringLiteral("two-audio.mkv")));

    // Discover the two audio track indices from the typed track list.
    QList<int> audioIndices;
    for (const QVariant &entry : session.tracks()) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("type")).toString() == QStringLiteral("audio"))
            audioIndices.append(map.value(QStringLiteral("index")).toInt());
    }
    require(audioIndices.size() == 2, "two-audio fixture did not expose two audio tracks");

    auto switchAndWait = [&](int streamIndex) {
        QEventLoop loop;
        bool done = false;
        quint64 expected = 0;
        auto c = QObject::connect(&session, &Player2Session::audioTrackChanged, &loop,
                                  [&](quint64 generation, int index) {
            if (generation == expected && index == streamIndex) { done = true; loop.quit(); }
        });
        QTimer::singleShot(3'000, &loop, &QEventLoop::quit);
        session.selectAudioTrack(QString::number(streamIndex));
        expected = session.generation();
        loop.exec();
        QObject::disconnect(c);
        require(done, "audio track switch did not complete");
    };

    switchAndWait(audioIndices[1]);            // during play
    session.pause();
    switchAndWait(audioIndices[0]);            // while paused
    session.play();
    seekAndWait(session, session.duration() / 3.0);
    switchAndWait(audioIndices[1]);            // immediately after a seek
    require(!guard.sawStale, "track switch leaked a stale packet");
    session.close();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    try {
        require(argc == 2, "usage: player2_seek_generation_test FIXTURE_DIRECTORY");
        const QDir fixtures(QString::fromLocal8Bit(argv[1]));
        require(fixtures.exists(), "fixture directory does not exist");
        hundredSeeksAreGenerationSafe(fixtures);
        seekNearBoundsIsExact(fixtures);
        realPauseSuspendsProgress(fixtures);
        frameStepOnPausedFixture(fixtures);
        audioTrackSwitchIsGenerationSafe(fixtures);
    } catch (const std::exception &error) {
        std::cerr << "player2_seek_generation_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_seek_generation_test: PASS\n";
    return EXIT_SUCCESS;
}
