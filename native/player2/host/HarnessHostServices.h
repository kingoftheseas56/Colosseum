#pragma once

#include "Player2HostServices.h"
#include "player2/video/D3D11VideoPipeline.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>

namespace Colosseum::Player2 {

class Player2VideoItem;

class HarnessHostServices final : public Player2HostServices
{
    Q_OBJECT
    Q_PROPERTY(QString adapter READ adapter NOTIFY metricsChanged)
    Q_PROPERTY(QString source READ source CONSTANT)
    Q_PROPERTY(QString decodePath READ decodePath NOTIFY metricsChanged)
    Q_PROPERTY(QString status READ status NOTIFY metricsChanged)
    Q_PROPERTY(quint64 generated READ generated NOTIFY metricsChanged)
    Q_PROPERTY(quint64 presented READ presented NOTIFY metricsChanged)
    Q_PROPERTY(quint64 dropped READ dropped NOTIFY metricsChanged)
    Q_PROPERTY(quint64 late READ late NOTIFY metricsChanged)
    Q_PROPERTY(quint64 cpuTransfers READ cpuTransfers NOTIFY metricsChanged)
    Q_PROPERTY(quint64 deviceErrors READ deviceErrors NOTIFY metricsChanged)
    Q_PROPERTY(bool adapterMatch READ adapterMatch NOTIFY metricsChanged)

public:
    explicit HarnessHostServices(QObject *parent = nullptr);

    QString adapter() const;
    QString source() const;
    QString decodePath() const;
    QString status() const;
    quint64 generated() const;
    quint64 presented() const;
    quint64 dropped() const;
    quint64 late() const;
    quint64 cpuTransfers() const;
    quint64 deviceErrors() const;
    bool adapterMatch() const;

    void setReportPath(const QString &path);
    bool startScenario(const QString &scenario, QString *error);
    Q_INVOKABLE void attachVideoItem(QObject *item);
    void requestAdjacentEpisode(const QString &mediaId, int direction) override;
    void requestAlternateSources(const QString &mediaId) override;
    void reportProgress(const QString &mediaId, double position, double duration) override;

signals:
    void metricsChanged();

private:
    void produceFrame();
    void refreshMetrics();
    void finish(bool passed, const QString &message, int exitCode);
    bool writeReport(bool passed, const QString &message) const;
    void appendEvent(const QString &event, const QString &message) const;

    D3D11VideoPipeline m_pipeline;
    QPointer<Player2VideoItem> m_item;
    QTimer m_frameTimer;
    QTimer m_watchdog;
    D3D11VideoPipeline::Diagnostics m_metrics;
    QString m_reportPath;
    QString m_status = QStringLiteral("Waiting for D3D11 scene graph");
    quint64 m_sequence = 0;
    bool m_started = false;
    bool m_finished = false;
};

} // namespace Colosseum::Player2
