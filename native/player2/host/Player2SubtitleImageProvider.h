#pragma once

#include "player2/core/Player2Session.h"

#include <QtQuick/QQuickImageProvider>

namespace Colosseum::Player2 {

// Serves the current bitmap (PGS/DVD) subtitle picture to QML. The QML source is
// image://player2subtitle/<id>, where <id> is the session's bitmap-cue id; a stale id (the cue already
// cleared) returns a null image. Qt calls requestImage on a dedicated image thread, so it only touches
// Player2Session::subtitleImageForProvider, which is mutex-guarded. The session owns the whole app
// lifetime here, so a raw pointer is safe.
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
