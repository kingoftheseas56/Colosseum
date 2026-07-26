#include "Player2VideoItem.h"

#include "D3D11VideoPipeline.h"

#include <QtCore/QMetaObject>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>
#include <QtQuick/qsgtexture_platform.h>

namespace Colosseum::Player2 {

Player2VideoItem::Player2VideoItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *quickWindow) {
        if (quickWindow && !m_afterRenderingConnected) {
            connect(quickWindow, &QQuickWindow::afterRendering, this,
                    &Player2VideoItem::afterRendering, Qt::DirectConnection);
            m_afterRenderingConnected = true;
        }
    });
}

Player2VideoItem::~Player2VideoItem() = default;

QObject *Player2VideoItem::session() const { return m_session.data(); }

void Player2VideoItem::setSession(QObject *session)
{
    if (m_session == session)
        return;
    m_session = session;
    emit sessionChanged();
}

void Player2VideoItem::setVideoPipeline(D3D11VideoPipeline *pipeline)
{
    if (m_pipeline == pipeline)
        return;
    m_pipeline = pipeline;
    m_initialized = false;
    update();
}

QString Player2VideoItem::errorString() const { return m_error; }

bool Player2VideoItem::initializeOnRenderThread()
{
    if (m_initialized)
        return true;
    QQuickWindow *quickWindow = window();
    if (!m_pipeline) {
        m_error = QStringLiteral("Player 2 video pipeline is not attached");
        return false;
    }
    if (!quickWindow || quickWindow->rendererInterface()->graphicsApi() !=
                            QSGRendererInterface::Direct3D11) {
        m_error = QStringLiteral("Qt Quick is not using Direct3D11");
        return false;
    }
    if (!m_afterRenderingConnected) {
        connect(quickWindow, &QQuickWindow::afterRendering, this,
                &Player2VideoItem::afterRendering, Qt::DirectConnection);
        m_afterRenderingConnected = true;
    }
    auto *device = static_cast<ID3D11Device *>(quickWindow->rendererInterface()->getResource(
        quickWindow, QSGRendererInterface::DeviceResource));
    if (!m_pipeline->initialize(device, &m_error))
        return false;
    for (std::size_t i = 0; i < m_textures.size(); ++i) {
        m_textures[i].reset(QNativeInterface::QSGD3D11Texture::fromNative(
            m_pipeline->consumerTexture(i), quickWindow, m_pipeline->textureSize()));
        if (!m_textures[i]) {
            m_error = QStringLiteral("Qt failed to wrap shared texture slot %1").arg(i);
            return false;
        }
    }
    m_initialized = true;
    return true;
}

QSGNode *Player2VideoItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node)
        node = new QSGSimpleTextureNode;
    node->setRect(boundingRect());
    if (!initializeOnRenderThread()) {
        const QString error = m_error;
        QMetaObject::invokeMethod(this, [this, error] { emit initializationFailed(error); },
                                  Qt::QueuedConnection);
        return node;
    }
    // Present the ring's CURRENT generation: seeks and track switches advance the ring's
    // generation under the item, and the frozen m_generation=1 this once passed made every
    // post-seek acquire fail — the picture froze on the last pre-seek frame while audio
    // played on (the seek-freeze bug).
    if (const auto frame = m_pipeline->acquirePresentationFrame()) {
        if (m_pipeline->waitForProducer(frame->token.sequence)) {
            node->setTexture(m_textures[frame->slot].get());
            m_pendingRetire = frame->retiringSlot;
            m_pipeline->notePresented();
            QMetaObject::invokeMethod(this, [this] { emit framePresented(); },
                                      Qt::QueuedConnection);
        }
    }
    return node;
}

void Player2VideoItem::afterRendering()
{
    if (m_pendingRetire && m_pipeline) {
        m_pipeline->retireAfterRendering(*m_pendingRetire);
        m_pendingRetire.reset();
    }
}

void Player2VideoItem::releaseResources()
{
    for (auto &texture : m_textures)
        texture.reset();
    m_initialized = false;
}

} // namespace Colosseum::Player2
