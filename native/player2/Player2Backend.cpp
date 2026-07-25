#include "player2/Player2Backend.h"

#include "player2/host/Player2SubtitleImageProvider.h"
#include "player2/video/Player2VideoItem.h"

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
    // A session that errors out is the engine telling us it cannot carry this playback. Whether that
    // is recoverable by handing over to mpvqt depends entirely on whether anything was shown yet.
    connect(&m_session, &Player2Session::stateChanged, this, [this]() {
        if (m_session.state() == Player2State::Error)
            reportFailure(QStringLiteral("the Player 2 session entered an error state"));
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

    m_session.open(playback);
    return result;
}

void Player2Backend::stop()
{
    m_session.close();
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
