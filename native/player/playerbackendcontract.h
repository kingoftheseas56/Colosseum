#ifndef COLOSSEUM_PLAYERBACKENDCONTRACT_H
#define COLOSSEUM_PLAYERBACKENDCONTRACT_H

#include <QString>
#include <QVariantMap>

// Host-facing operations every playback backend must provide. The concrete
// backend remains a QQuickItem/QML type, but shared host code can rely on this
// lifecycle/source contract without depending on MpvQt.
class PlayerBackendContract
{
public:
    virtual ~PlayerBackendContract() = default;

    virtual QVariantMap capabilities() const = 0;
    virtual void loadSource(const QString &url, const QVariantMap &headers) = 0;
    virtual void stopPlayback() = 0;

    // Android uses these when the Activity/surface is recreated and when audio
    // focus changes. Desktop backends may intentionally implement them as no-ops.
    virtual void setHostLifecycleState(const QString &state) = 0;
    virtual void setAudioFocusState(const QString &state) = 0;
    virtual void releaseVideoSurface() = 0;
    virtual void restoreVideoSurface() = 0;
};

#endif // COLOSSEUM_PLAYERBACKENDCONTRACT_H
