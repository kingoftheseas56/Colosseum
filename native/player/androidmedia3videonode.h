#ifndef COLOSSEUM_ANDROIDMEDIA3VIDEONODE_H
#define COLOSSEUM_ANDROIDMEDIA3VIDEONODE_H

#include <QtCore/qglobal.h>

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QRectF>
#include <QSize>
#include <QSGRenderNode>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

// Render-thread-only external-OES node used by the later AndroidMedia3Item facade.
// Playback state remains outside this class. The facade only supplies scheduling and
// generation-tagged Media3 Surface attach/detach callbacks.
class AndroidMedia3VideoNode final : public QSGRenderNode
{
public:
    using FrameScheduleCallback = std::function<void()>;
    using SurfaceReadyCallback = std::function<void(quint64, const QJniObject &)>;
    using ClearSurfaceBlockingCallback = std::function<void(quint64, const QJniObject &)>;

    AndroidMedia3VideoNode(jlong nativeHandle,
                           FrameScheduleCallback scheduleFrame,
                           SurfaceReadyCallback surfaceReady,
                           ClearSurfaceBlockingCallback clearSurfaceBlocking);
    ~AndroidMedia3VideoNode() override;

    RenderingFlags flags() const override;
    QRectF rect() const override;
    void render(const RenderState *state) override;
    void releaseResources() override;

    void setTargetRect(const QRectF &rect);
    void setVideoSize(const QSize &videoSize);
    quint64 surfaceGeneration() const;

private:
    bool ensureResources();
    void destroyGlObjects();
    QRectF aspectFitRect() const;
    void unregisterFrameCallback();

    jlong m_nativeHandle = 0;
    quint64 m_registrySerial = 0;
    quint64 m_surfaceGeneration = 0;
    QRectF m_targetRect;
    QSize m_videoSize;
    FrameScheduleCallback m_scheduleFrame;
    SurfaceReadyCallback m_surfaceReady;
    ClearSurfaceBlockingCallback m_clearSurfaceBlocking;
    std::shared_ptr<std::atomic<quint64>> m_pendingFrameGeneration;
    QJniObject m_bridge;
    bool m_surfacePublished = false;

    unsigned int m_program = 0;
    unsigned int m_textureId = 0;
    unsigned int m_vertexBuffer = 0;
    int m_mvpLocation = -1;
    int m_textureLocation = -1;
    int m_opacityLocation = -1;
    std::array<float, 16> m_transform{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
};

#endif // Q_OS_ANDROID

#endif // COLOSSEUM_ANDROIDMEDIA3VIDEONODE_H
