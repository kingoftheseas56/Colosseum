#pragma once

#include "Player2HostServices.h"
#include "player2/core/Player2Session.h"
#include "player2/video/D3D11VideoPipeline.h"

#include <QtCore/QObject>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>


namespace Colosseum::Player2 {

class Player2VideoItem;

class HarnessHostServices final : public Player2HostServices
{
    Q_OBJECT
    Q_PROPERTY(QString adapter READ adapter NOTIFY metricsChanged)
    Q_PROPERTY(QString source READ source NOTIFY metricsChanged)
    Q_PROPERTY(QString decodePath READ decodePath NOTIFY metricsChanged)
    Q_PROPERTY(QString status READ status NOTIFY metricsChanged)
    Q_PROPERTY(quint64 generated READ generated NOTIFY metricsChanged)
    Q_PROPERTY(quint64 presented READ presented NOTIFY metricsChanged)
    Q_PROPERTY(quint64 dropped READ dropped NOTIFY metricsChanged)
    Q_PROPERTY(quint64 late READ late NOTIFY metricsChanged)
    Q_PROPERTY(quint64 cpuTransfers READ cpuTransfers NOTIFY metricsChanged)
    Q_PROPERTY(quint64 deviceErrors READ deviceErrors NOTIFY metricsChanged)
    Q_PROPERTY(bool adapterMatch READ adapterMatch NOTIFY metricsChanged)
    Q_PROPERTY(QString sessionState READ sessionState NOTIFY metricsChanged)
    Q_PROPERTY(double duration READ duration NOTIFY metricsChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY metricsChanged)

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
    QString sessionState() const;
    double duration() const;
    int trackCount() const;

    void setReportPath(const QString &path);
    void setMinimumRunSeconds(int seconds);
    void setNormalizationMode(NormalizationMode mode);
    bool startScenario(const QString &scenario, QString *error);
    bool startFile(const QString &path, QString *error);
    bool startUrl(const QString &url, const QString &headersJsonPath, bool live, QString *error);
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
    Player2Session m_session;
    QPointer<Player2VideoItem> m_item;
    QTimer m_frameTimer;
    QTimer m_watchdog;
    bool hasMedia() const { return !m_filePath.isEmpty() || !m_url.isEmpty(); }

    D3D11VideoPipeline::Diagnostics m_metrics;
    QString m_reportPath;
    QString m_filePath;
    QString m_url;
    RequestHeaders m_headers;
    bool m_live = false;
    QString m_status = QStringLiteral("Waiting for D3D11 scene graph");
    quint64 m_sequence = 0;
    double m_reportDuration = 0.0;
    int m_reportTrackCount = 0;
    QString m_reportVideoCodec;
    QString m_reportAudioDevice;
    QString m_reportAudioFormat;
    double m_maxAudioQueueMs = 0.0;
    double m_finalAudioQueueMs = 0.0;
    QString m_reportNormalization = QStringLiteral("Smooth");
    double m_reportNormalizationLatencyMs = 0.0;
    quint64 m_reportAudioUnderruns = 0;
    bool m_sawAudioClock = false;
    double m_reportAvP95Ms = 0.0;
    int m_minimumRunSeconds = 0;
    QElapsedTimer m_runTimer;
    bool m_fileOpened = false;
    bool m_sawPlaying = false;
    bool m_started = false;
    bool m_finished = false;
};

} // namespace Colosseum::Player2
