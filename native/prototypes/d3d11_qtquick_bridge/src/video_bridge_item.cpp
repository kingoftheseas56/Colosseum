#include "video_bridge_item.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaObject>
#include <QtCore/QStringList>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>
#include <QtQuick/qsgtexture_platform.h>

VideoBridgeItem::VideoBridgeItem(QQuickItem *parent)
    : QQuickItem(parent), m_producer(&m_bridge)
{
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *newWindow) {
        if (newWindow) {
            connect(newWindow, &QQuickWindow::afterRendering, this,
                    &VideoBridgeItem::afterRendering, Qt::DirectConnection);
        }
    });
}

VideoBridgeItem::~VideoBridgeItem()
{
    m_producer.stop();
}

bool VideoBridgeItem::initializeOnRenderThread()
{
    if (m_initialized)
        return true;
    QQuickWindow *quickWindow = window();
    if (!quickWindow || quickWindow->rendererInterface()->graphicsApi() != QSGRendererInterface::Direct3D11) {
        m_error = QStringLiteral("Qt Quick is not using Direct3D11");
        return false;
    }
    auto *device = static_cast<ID3D11Device *>(quickWindow->rendererInterface()->getResource(
        quickWindow, QSGRendererInterface::DeviceResource));
    if (!m_bridge.initializeConsumer(device, m_error))
        return false;

    for (std::size_t i = 0; i < m_textures.size(); ++i) {
        m_textures[i].reset(QNativeInterface::QSGD3D11Texture::fromNative(
            m_bridge.consumerTexture(i), quickWindow, m_bridge.textureSize()));
        if (!m_textures[i]) {
            m_error = QStringLiteral("Qt failed to wrap shared texture slot %1").arg(i);
            return false;
        }
    }
    m_initialized = true;
    return true;
}

QSGNode *VideoBridgeItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node)
        node = new QSGSimpleTextureNode;
    node->setRect(boundingRect());

    if (!initializeOnRenderThread()) {
        QMetaObject::invokeMethod(this, [this] { emit statusChanged(); }, Qt::QueuedConnection);
        return node;
    }
    if (!m_started) {
        m_started = true;
        const auto wakeConsumer = [this] {
            QMetaObject::invokeMethod(this, [this] {
                update();
                emit statusChanged();
            }, Qt::QueuedConnection);
        };
        if (m_source == QStringLiteral("hevc"))
            m_producer.startHevc(m_file, wakeConsumer);
        else
            m_producer.startSynthetic(24.0, wakeConsumer);
    }

    if (const auto selection = m_bridge.acquireLatestForConsumer()) {
        if (m_bridge.waitForProducer(selection->sequence)) {
            node->setTexture(m_textures[selection->slot].get());
            m_pendingRetire = selection->retiringSlot;
            m_bridge.notePresented();
        }
    } else if (node->texture()) {
        m_bridge.noteRepeated();
    }
    return node;
}

void VideoBridgeItem::afterRendering()
{
    if (m_pendingRetire) {
        m_bridge.afterFrameSubmitted(*m_pendingRetire);
        m_pendingRetire.reset();
    }
}

QString VideoBridgeItem::statusText() const
{
    if (!m_error.isEmpty())
        return QStringLiteral("FAILED: %1").arg(m_error);
    const auto s = m_bridge.snapshot();
    QStringList lines{
        QStringLiteral("Source: %1   Codec: %2   HW: %3").arg(s.source, s.codec, s.hardwareFormat),
        QStringLiteral("Input: %1   Size: %2").arg(s.inputFormat, s.sourceSize),
        QStringLiteral("Qt: %1").arg(s.graphicsApi),
        QStringLiteral("Qt adapter: %1").arg(s.qtAdapter),
        QStringLiteral("Producer: %1").arg(s.producerAdapter),
        QStringLiteral("Adapter match: %1   Shared fences: %2")
            .arg(s.adapterMatch ? QStringLiteral("yes") : QStringLiteral("no"),
                 s.sharedFences ? QStringLiteral("yes") : QStringLiteral("no")),
        QStringLiteral("Decoded: %1   Converted: %2   Presented: %3")
            .arg(s.decoded).arg(s.converted).arg(s.presented),
        QStringLiteral("Repeated: %1   Dropped: %2   Late: %3")
            .arg(s.repeated).arg(s.dropped).arg(s.late),
        QStringLiteral("Device errors: %1").arg(s.deviceErrors),
        QStringLiteral("Producer fence: %1   Consumer fence: %2")
            .arg(s.producerFence).arg(s.consumerFence),
        QStringLiteral("CPU frame transfers: %1").arg(s.cpuTransfers)
    };
    if (!s.sourceError.isEmpty())
        lines << QStringLiteral("FAILED: %1").arg(s.sourceError);
    return lines.join(QLatin1Char('\n'));
}

bool VideoBridgeItem::writeReport(const QString &path) const
{
    const auto s = m_bridge.snapshot();
    QJsonObject report{
        {QStringLiteral("graphicsApi"), s.graphicsApi},
        {QStringLiteral("qtAdapter"), s.qtAdapter},
        {QStringLiteral("producerAdapter"), s.producerAdapter},
        {QStringLiteral("adapterMatch"), s.adapterMatch},
        {QStringLiteral("sharedFences"), s.sharedFences},
        {QStringLiteral("generated"), static_cast<qint64>(s.generated)},
        {QStringLiteral("presented"), static_cast<qint64>(s.presented)},
        {QStringLiteral("producerStarved"), static_cast<qint64>(s.producerStarved)},
        {QStringLiteral("cpuTransfers"), static_cast<qint64>(s.cpuTransfers)},
        {QStringLiteral("deviceErrors"), static_cast<qint64>(s.deviceErrors)},
        {QStringLiteral("producerFence"), static_cast<qint64>(s.producerFence)},
        {QStringLiteral("consumerFence"), static_cast<qint64>(s.consumerFence)},
        {QStringLiteral("error"), m_error}
        ,{QStringLiteral("source"), s.source}
        ,{QStringLiteral("codec"), s.codec}
        ,{QStringLiteral("hardwareFormat"), s.hardwareFormat}
        ,{QStringLiteral("inputFormat"), s.inputFormat}
        ,{QStringLiteral("sourceSize"), s.sourceSize}
        ,{QStringLiteral("sourceError"), s.sourceError}
        ,{QStringLiteral("decoded"), static_cast<qint64>(s.decoded)}
        ,{QStringLiteral("converted"), static_cast<qint64>(s.converted)}
        ,{QStringLiteral("dropped"), static_cast<qint64>(s.dropped)}
        ,{QStringLiteral("late"), static_cast<qint64>(s.late)}
        ,{QStringLiteral("repeated"), static_cast<qint64>(s.repeated)}
        ,{QStringLiteral("published"), static_cast<qint64>(s.generated)}
        ,{QStringLiteral("softwareFallback"), s.softwareFallback}
    };
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) >= 0;
}

void VideoBridgeItem::releaseResources()
{
    m_producer.stop();
    for (auto &texture : m_textures)
        texture.reset();
    m_bridge.shutdown();
    m_initialized = false;
    m_started = false;
}
