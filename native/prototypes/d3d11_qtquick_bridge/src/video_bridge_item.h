#pragma once

#include "frame_producer.h"
#include "shared_bridge.h"

#include <QtQuick/QQuickItem>

#include <array>
#include <memory>
#include <optional>

class QSGTexture;

class VideoBridgeItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    QML_ELEMENT

public:
    explicit VideoBridgeItem(QQuickItem *parent = nullptr);
    ~VideoBridgeItem() override;

    QString statusText() const;
    Q_INVOKABLE bool writeReport(const QString &path) const;

signals:
    void statusChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;

private:
    bool initializeOnRenderThread();
    void afterRendering();

    SharedBridge m_bridge;
    FrameProducer m_producer;
    std::array<std::unique_ptr<QSGTexture>, SlotRing::SlotCount> m_textures;
    std::optional<std::size_t> m_pendingRetire;
    QString m_error;
    bool m_initialized = false;
    bool m_started = false;
};
