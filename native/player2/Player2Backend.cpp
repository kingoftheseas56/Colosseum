#include "player2/Player2Backend.h"

#include "player2/host/Player2SubtitleImageProvider.h"
#include "player2/video/Player2VideoItem.h"

#include <QtCore/QMetaEnum>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtQml/QQmlEngine>

namespace Colosseum::Player2
{
namespace
{
QString outcomeName(BackendOutcome outcome)
{
    switch (outcome) {
    case BackendOutcome::UsePlayer2:
        return QStringLiteral("player2");
    case BackendOutcome::UseMpvQt:
        return QStringLiteral("mpvqt");
    case BackendOutcome::FallbackToMpvQt:
        return QStringLiteral("fallback");
    case BackendOutcome::TerminalError:
        break;
    }
    return QStringLiteral("error");
}
}

Player2Backend::Player2Backend(QObject *parent)
    : QObject(parent)
{
    // Give the session the video pipeline decoded frames are submitted INTO. Without this the session
    // holds a null pipeline: audio plays perfectly (it owns a separate AudioPipeline) while every
    // decoded video frame goes nowhere, the item is never handed a texture, and the picture stays
    // black - which is exactly how the first real Player 2 playback behaved (2026-07-25).
    m_session.setVideoPipeline(&m_pipeline);

    // ~60Hz pump: keeps the video item repainting and opens a pending request the moment the D3D11
    // pipeline reports a matching adapter. Same shape as the lab's frame timer.
    m_pump.setInterval(16);
    connect(&m_pump, &QTimer::timeout, this, &Player2Backend::pump);
    // The engine says WHY it failed; report that verbatim rather than a generic line. Surfacing
    // "the session entered an error state" told nobody anything when seeking on a torrent kept
    // failing (2026-07-25) - the code and message are what make the next failure diagnosable.
    //
    // transition(Player2State::Error) emits stateChanged() BEFORE emitting this signal (both
    // synchronously, same call stack), so stateChanged always sees the failure first with no
    // message yet attached - that ordering is why this handler, not stateChanged, has to be the one
    // that actually reports. errorOccurred also fires for a REJECTED transition (e.g. a stray
    // command arriving late) with no state change at all; that is not a failure to report, so it is
    // gated on the session actually being in Error. m_failureReported blocks a second rejection (or
    // stateChanged's net below) from re-reporting/overwriting the real cause once it has landed.
    connect(&m_session, &Player2Session::errorOccurred, this, [this](const Player2Error &error) {
        const QString name = QString::fromLatin1(
            QMetaEnum::fromType<Player2ErrorCode>().valueToKey(static_cast<int>(error.code)));
        const QString message = error.message.isEmpty()
            ? QStringLiteral("Playback failed (%1)").arg(name)
            : QStringLiteral("%1 (%2)").arg(error.message, name);
        if (m_session.state() != Player2State::Error || m_failureReported)
            return;
        m_lastError = message;
        m_failureReported = true;
        // Deferred: this signal can fire from inside Player2Session::transition(), and reportFailure
        // can emit fallbackRequested, which QML answers by swapping the player Loader - destroying
        // this session (and us) while transition()'s own frame is still on the stack.
        QTimer::singleShot(0, this, [this, message] { reportFailure(message); });
    });
    // A session that errors out is the engine telling us it cannot carry this playback. Whether that
    // is recoverable by handing over to mpvqt depends entirely on whether anything was shown yet.
    // stateChanged fires synchronously the instant Error is entered, ahead of errorOccurred above -
    // so this is only a net for an Error with no error signal at all, and it must defer so the real
    // message (if one arrives on the same tick) gets to report first; the guard on the far side of
    // the timer makes this a no-op once errorOccurred already has.
    connect(&m_session, &Player2Session::stateChanged, this, [this]() {
        if (m_session.state() != Player2State::Error)
            return;
        QTimer::singleShot(0, this, [this] {
            if (m_failureReported || m_session.state() != Player2State::Error)
                return;
            m_failureReported = true;
            reportFailure(QStringLiteral("the Player 2 session entered an error state"));
        });
    });
}

QObject *Player2Backend::sessionObject()
{
    return &m_session;
}

bool Player2Backend::available() const
{
    // Reaching this code at all means Player 2 was linked into the binary. Whether THIS machine's
    // adapter can actually carry it is not knowable until a surface exists, so an unusable adapter
    // surfaces later as an initialization failure and takes the fallback path — it is not guessed at
    // here. (Ground-truth over prediction; the router still gets the honest answer, just later.)
    return true;
}

void Player2Backend::attachVideoItem(QObject *object)
{
    auto *item = qobject_cast<Player2VideoItem *>(object);
    if (!item) {
        reportFailure(QStringLiteral("the Player 2 video surface was not a Player2VideoItem"));
        return;
    }
    m_item = item;
    item->setSession(&m_session);
    item->setVideoPipeline(&m_pipeline);
    connect(item, &Player2VideoItem::initializationFailed, this,
            [this](const QString &message) { reportFailure(message); });

    // PGS/DVD bitmap subtitles are served to QML as image://player2subtitle/<id>, which needs THIS
    // session. In the lab that provider is installed at startup because the harness owns the session
    // before the engine loads; in production the session lives here, created from QML, so the earliest
    // honest moment to install it is now — when we are demonstrably inside a live engine.
    if (QQmlEngine *engine = qmlEngine(this)) {
        engine->addImageProvider(QStringLiteral("player2subtitle"),
                                 new Player2SubtitleImageProvider(&m_session));
    }

    item->update();
}

QVariantMap Player2Backend::play(const QVariantMap &request)
{
    PlaybackRequest playback;
    playback.source = QUrl(request.value(QStringLiteral("url")).toString());
    playback.mediaId = request.value(QStringLiteral("mediaId")).toString();
    playback.title = request.value(QStringLiteral("title")).toString();
    playback.resumeSeconds = request.value(QStringLiteral("resumeSeconds"), 0.0).toDouble();
    playback.live = request.value(QStringLiteral("live"), false).toBool();
    playback.stream = !playback.source.isLocalFile();

    // Debrid and other direct links carry their authorization in headers; the engine's HTTP source
    // already honours them, so this is a pass-through, not a new transport path.
    const QVariantMap headers = request.value(QStringLiteral("headers")).toMap();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        playback.headers.insert(it.key().toUtf8(), it.value().toString().toUtf8());

    const Player2Capabilities capabilities{/*compiledIn=*/true, /*adapterUsable=*/true};
    const BackendDecision decision = chooseBackend(playback, PlayerBackend::Player2, capabilities);

    QVariantMap result;
    result.insert(QStringLiteral("outcome"), outcomeName(decision.outcome));
    result.insert(QStringLiteral("reason"), decision.reason);

    if (decision.outcome != BackendOutcome::UsePlayer2)
        return result;

    // Queue it; pump() opens once the GPU side is genuinely up. See the note on m_pending.
    m_lastError.clear();
    m_failureReported = false;
    m_pending = playback;
    m_hasPending = true;
    m_waitTicks = 0;
    m_pump.start();
    return result;
}

void Player2Backend::stop()
{
    m_hasPending = false;
    m_failureReported = false;
    m_pump.stop();
    m_session.close();
}

void Player2Backend::pump()
{
    if (!m_item)
        return;
    // Repaint drives the pipeline's initialisation and, once playing, its presentation.
    m_item->update();

    if (!m_hasPending)
        return;

    if (!m_pipeline.diagnostics().adapterMatch) {
        // Give the scene graph a bounded chance to bring the device up (~5s at 16ms). If it never
        // does, say so rather than sit forever on a black page.
        if (++m_waitTicks > 300) {
            m_hasPending = false;
            reportFailure(QStringLiteral("the Player 2 video device never became ready"));
        }
        return;
    }

    m_hasPending = false;
    m_session.open(m_pending);
}

bool Player2Backend::firstFrameSeen() const
{
    return m_pipeline.diagnostics().presented > 0;
}

void Player2Backend::reportFailure(const QString &reason)
{
    if (classifyRuntimeFailure(firstFrameSeen()) == FailureResponse::FallbackToMpvQt) {
        emit fallbackRequested(reason);
        return;
    }
    emit restartRequired(reason);
}
}
