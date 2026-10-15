#pragma once

#include "player2/PlayerBackendRouter.h"
#include "player2/core/Player2Session.h"
#include "player2/video/D3D11VideoPipeline.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>

namespace Colosseum::Player2
{
class Player2VideoItem;

// Player2Backend is what the APP holds when it plays through Player 2: it owns the engine session and
// the D3D11 pipeline, turns the app's play request into a typed PlaybackRequest, and asks the router
// whether Player 2 should be handling this playback at all.
//
// It exists because the engine's open() takes a C++ gadget that QML cannot reasonably build, and
// because somebody in production has to own the session's lifetime the way the lab's harness does.
// It is deliberately thin: no catalog, no source policy, no persistence — all of that is the host
// services' job (qml/player2/ColosseumHostServices.qml). This class only starts and stops playback.
// Not `final`: qmlRegisterType generates a subclass, so a final backend cannot be created from QML.
class Player2Backend : public QObject
{
    Q_OBJECT
    // The live session, handed straight to the shell (which drives play/pause/seek on it as slots).
    Q_PROPERTY(QObject *session READ sessionObject CONSTANT)
    // Whether this binary can play through Player 2 at all (i.e. it was linked in).
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit Player2Backend(QObject *parent = nullptr);

    QObject *sessionObject();
    bool available() const;

    // Bind the QML video surface to the engine — the production twin of the lab's attachVideoItem.
    Q_INVOKABLE void attachVideoItem(QObject *item);

    // Ask the router, and open only if it says Player 2 owns this playback. The returned map is
    // { "outcome": "player2"|"mpvqt"|"fallback"|"error", "reason": "..." } so QML can route without
    // re-deriving the policy. Accepted request keys: url, mediaId, title, resumeSeconds, live,
    // headers (a map of string->string, which is how debrid/direct links carry their auth).
    Q_INVOKABLE QVariantMap play(const QVariantMap &request);

    Q_INVOKABLE void stop();

signals:
    // Player 2 could not carry this playback and nothing had been shown yet — the app should hand this
    // same playback to mpvqt. Carries the reason so it can reach diagnostics rather than vanishing.
    void fallbackRequested(const QString &reason);
    // Player 2 failed with the picture already up. Swapping backends now would put two clocks on one
    // playback, so the app must surface it and start a NEW session instead. (Plan's explicit rule.)
    void restartRequired(const QString &reason);

private:
    // One failure per playback attempt, claimed synchronously (so an already-armed net cannot beat
    // it) and reported from the event loop (so no caller's frame - transition(), m_pump's timeout -
    // is still alive when a fallback destroys us). A play()/stop() in between cancels the report.
    void claimFailure(const QString &reason);
    // Route a failure by whether anything has been presented yet — see classifyRuntimeFailure.
    void reportFailure(const QString &reason);
    bool firstFrameSeen() const;
    // Drives the render loop and opens the pending request once the GPU side is genuinely up.
    void pump();

    // Declared (and therefore destroyed) before m_session: the session holds a raw
    // D3D11VideoPipeline* (Player2Session.h:173) and must be torn down first, or it is left holding
    // a dangling pointer during ~Player2Backend. Keep this ordering if the members are ever reshuffled.
    D3D11VideoPipeline m_pipeline;
    Player2Session m_session;
    QPointer<Player2VideoItem> m_item;

    // A request may NOT be opened until the D3D11 pipeline has a live device (adapterMatch). Opening
    // earlier gives the decoder no hardware context: the file opens, duration and codec read fine,
    // and then nothing decodes — audio plays on its own pipeline while the picture stays black. The
    // lab avoids this by only opening from its frame tick once adapterMatch is true; this mirrors it.
    QTimer m_pump;
    PlaybackRequest m_pending;
    bool m_hasPending = false;
    int m_waitTicks = 0;
    quint64 m_lastSubmitted = 0; // pump(): repaint only when the pipeline moved

    // Whether a failure has already been claimed/reported for the CURRENT playback attempt — reset
    // in play() and stop(). This is what stops a harmless rejected-transition signal, the generic
    // stateChanged net, or a late device-init/device-never-ready failure from double-reporting or
    // overwriting a cause that has already gone out.
    bool m_failureReported = false;
};
}
