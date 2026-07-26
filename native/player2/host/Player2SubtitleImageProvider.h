#pragma once

#include "player2/core/Player2Session.h"

#include <QtCore/QPointer>
#include <QtQuick/QQuickImageProvider>

namespace Colosseum::Player2 {

// Serves the current bitmap (PGS/DVD) subtitle picture to QML. The QML source is
// image://player2subtitle/<id>, where <id> is the session's bitmap-cue id; a stale id (the cue already
// cleared) returns a null image. Qt calls requestImage on a dedicated image thread, so it only touches
// Player2Session::subtitleImageForProvider, which is mutex-guarded.
//
// The handle to the session is a WEAK QPointer, not a raw pointer: the QQmlEngine owns this
// provider for the life of the engine, but the session dies with the page (Player2Backend is
// created/destroyed per playback), so a provider can outlive its session. QPointer self-nulls when
// the tracked QObject is destroyed, so the COMMON case - a stale image request arriving after the
// page has already closed - is answered correctly with an empty QImage() instead of touching freed
// memory. (We do NOT remove the provider from the engine on session teardown: addImageProvider
// replaces any existing provider registered under the same id, so a naive
// removeImageProvider("player2subtitle") in a later-installed Player2Backend's destructor could tear
// out a NEWER page's live provider instead of the old one. The weak handle sidesteps that specific
// trap - nothing needs to be removed.)
//
// What this does NOT close: requestImage runs on Qt's image thread while ~Player2Session runs on the
// GUI thread, and QPointer has no atomic promote-to-strong. Nothing stops the destructor completing
// BETWEEN the null-check below and the subtitleImageForProvider() call on the same line. That window
// is a genuine use-after-free (a destroyed QMutex, on freed memory) that this change narrows but does
// not close.
// The real fix is to stop sharing Player2Session across the thread boundary at all: a small shared
// state object (the mutex + the current subtitle image) owned by both Player2Session and this
// provider via std::shared_ptr, so the provider never dereferences the session itself. Not done here
// - flagged for whoever next touches this file.
class Player2SubtitleImageProvider : public QQuickImageProvider
{
public:
    explicit Player2SubtitleImageProvider(const Player2Session *session)
        : QQuickImageProvider(QQuickImageProvider::Image), m_session(const_cast<Player2Session *>(session))
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
    // Non-const: QPointer requires a non-const QObject type. Held and used read-only.
    QPointer<Player2Session> m_session;
};

} // namespace Colosseum::Player2
