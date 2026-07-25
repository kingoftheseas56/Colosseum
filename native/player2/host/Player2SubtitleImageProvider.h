#pragma once

#include "player2/core/Player2Session.h"

#include <QtQuick/QQuickImageProvider>

namespace Colosseum::Player2 {

// Serves the current bitmap (PGS/DVD) subtitle picture to QML. The QML source is
// image://player2subtitle/<id>, where <id> is the session's bitmap-cue id; a stale id (the cue already
// cleared) returns a null image. Qt calls requestImage on a dedicated image thread, so it only touches
// Player2Session::subtitleImageForProvider, which is mutex-guarded. The raw m_session pointer is only
// safe while the owning Player2Backend is alive: in production the session is created from QML and
// dies with the page, not the whole app lifetime as in the lab. This provider must be removed from
// the QQmlEngine when its Player2Backend is destroyed; that removal is not wired up yet (a later
// task owns it) — until it is, a provider can outlive its session. Note for whoever wires it up:
// removal by id is NOT symmetrical with installation. addImageProvider replaces any existing
// provider registered under the same id, so if a second page's Player2Backend installs its provider
// before the first page's destructor runs, a naive removeImageProvider("player2subtitle") in
// ~Player2Backend tears out the second page's LIVE provider, not the first's dead one. Removing by a
// weak handle to THIS instance (e.g. comparing against a QPointer captured at install time) avoids
// that trap; removing by the fixed id string does not.
class Player2SubtitleImageProvider : public QQuickImageProvider
{
public:
    explicit Player2SubtitleImageProvider(const Player2Session *session)
        : QQuickImageProvider(QQuickImageProvider::Image), m_session(session)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize & /*requested*/) override
    {
        QImage image = m_session ? m_session->subtitleImageForProvider(id) : QImage();
        if (size)
            *size = image.size();
        return image;
    }

private:
    const Player2Session *m_session;
};

} // namespace Colosseum::Player2
