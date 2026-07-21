#pragma once

#include "D3D11TextureRing.h"

#include <QtCore/QPointer>
#include <QtQuick/QQuickItem>

#include <array>
#include <memory>
#include <optional>

class QSGTexture;

namespace Colosseum::Player2 {

class D3D11VideoPipeline;

class Player2VideoItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *session READ session WRITE setSession NOTIFY sessionChanged)

public:
    explicit Player2VideoItem(QQuickItem *parent = nullptr);
    ~Player2VideoItem() override;

    QObject *session() const;
    void setSession(QObject *session);
    void setVideoPipeline(D3D11VideoPipeline *pipeline);
    QString errorString() const;

signals:
    void sessionChanged();
    void framePresented();
    void initializationFailed(const QString &message);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;

private:
    bool initializeOnRenderThread();
    void afterRendering();

    QPointer<QObject> m_session;
    D3D11VideoPipeline *m_pipeline = nullptr;
    std::array<std::unique_ptr<QSGTexture>, D3D11TextureRing::SlotCount> m_textures;
    std::optional<std::size_t> m_pendingRetire;
    QString m_error;
    quint64 m_generation = 1;
    bool m_initialized = false;
    bool m_afterRenderingConnected = false;
};

} // namespace Colosseum::Player2
